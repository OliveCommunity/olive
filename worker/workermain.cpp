/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include <cstdio>
#include <cstring>
#include <csignal>
#include <memory>
#include <optional>

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#ifdef Q_OS_LINUX
#include <execinfo.h>
#include <unistd.h>
#endif

#include "common/qtutils.h"
#include "config/config.h"
#include "coreengine.h"
#include "node/factory.h"
#include "node/input/multicam/multicamnode.h"
#include "node/project/serializer/serializer.h"
#include "render/diskmanager.h"
#include "render/framemanager.h"
#include "render/ipc/frameslotpool.h"
#include "render/ipc/ipcmessage.h"
#include "render/ipc/sharedmemoryregion.h"
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#endif
#include "render/opengl/openglrenderer.h"
#include "render/rendermanager.h"
#include "render/renderprocessor.h"
#include "render/colorprocessor.h"
#include "render/colortransform.h"

#ifdef Q_OS_MACOS
void HideWorkerDockIcon();
#endif

namespace
{

#ifdef Q_OS_LINUX
void print_backtrace(int sig)
{
	void *array[50];
	size_t size = backtrace(array, 50);
	fprintf(stderr, "worker: caught signal %d, backtrace:\n", sig);
	backtrace_symbols_fd(array, size, STDERR_FILENO);
	fflush(stderr);
	_exit(128 + sig);
}
#endif

constexpr int k_protocol_version = 1;
constexpr int k_default_width = 1920;
constexpr int k_default_height = 1080;
constexpr int k_default_frame_rate = 24;

void install_surface_format()
{
	QSurfaceFormat format;
	format.setVersion(3, 2);
	format.setProfile(QSurfaceFormat::CoreProfile);
	format.setDepthBufferSize(24);
	QSurfaceFormat::setDefaultFormat(format);
}

void log_error(const QString &message)
{
	const QByteArray line = QByteArray("worker: ") + message.toUtf8() + '\n';
	fwrite(line.constData(), 1, size_t(line.size()), stderr);
	fflush(stderr);
}

QJsonObject error_message(const QString &message, qint64 ticket_id = 0)
{
	QJsonObject o;
	o["type"] = olive::ipc::msgtype::k_error;
	o["message"] = message;
	if (ticket_id) {
		o["ticket"] = double(ticket_id);
	}
	return o;
}

class RenderWorker {
public:
	RenderWorker(olive::Renderer *renderer, QFile *out)
		: renderer_(renderer)
		, out_(out)
	{
	}

	~RenderWorker()
	{
		project_.reset();
		olive::ProjectSerializer::destroy();
		olive::DiskManager::destroy_instance();
		olive::FrameManager::destroy_instance();
		olive::NodeFactory::destroy();
	}

	bool initialize_runtime()
	{
		// Create a minimal EngineCore instance so that code paths calling
		// EngineCore::instance() (e.g. ViewerOutput::data for timecode display)
		// do not dereference null. The worker has no UI, so the plain engine
		// core is sufficient. The worker is short-lived; leaking this on exit
		// is harmless.
		if (!olive::EngineCore::instance()) {
			new olive::EngineCore(olive::EngineCore::CoreParams());
		}

		olive::Config::load();
		olive::NodeFactory::initialize();
		olive::ColorManager::set_up_default_config();
		olive::FrameManager::create_instance();
		olive::DiskManager::create_instance();
		olive::ProjectSerializer::initialize();
		return true;
	}

	bool send_startup_handshake()
	{
		olive::ipc::HandshakeMsg hs;
		hs.protocol_version = k_protocol_version;
		hs.shm_key = QString();
		hs.input_shm_key = QString();
		hs.input_slots = 0;
		hs.output_slots = 0;
		hs.slot_data_bytes = 0;
		hs.input_slot_data_bytes = 0;

		QJsonObject handshake = hs.to_json();
		QOpenGLContext *ctx = nullptr;
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		if (auto *dynamic_renderer =
				dynamic_cast<olive::DynamicRenderer *>(renderer_)) {
			ctx = dynamic_renderer->open_gl_context();
		} else
#endif
		{
			ctx = static_cast<olive::OpenGLRenderer *>(renderer_)->context();
		}
		if (ctx) {
			const QSurfaceFormat fmt = ctx->format();
			handshake["gl_major"] = fmt.majorVersion();
			handshake["gl_minor"] = fmt.minorVersion();
		}

		return write(handshake);
	}

	bool handle(const QJsonObject &message)
	{
		const QString type = message["type"].toString();

		if (type == QLatin1String(olive::ipc::msgtype::k_handshake)) {
			olive::ipc::HandshakeMsg hs;
			if (!olive::ipc::HandshakeMsg::from_json(message, &hs)) {
				return write(
					error_message(QStringLiteral("invalid handshake message")));
			}
			return attach_output_pool(hs);
		}

		if (type == QLatin1String(olive::ipc::msgtype::k_load_graph)) {
			olive::ipc::LoadGraphMsg load;
			if (!olive::ipc::LoadGraphMsg::from_json(message, &load)) {
				return write(
					error_message(QStringLiteral("invalid load_graph message")));
			}
			return load_graph(load.path);
		}

		if (type == QLatin1String(olive::ipc::msgtype::k_render_frame)) {
			olive::ipc::RenderFrameMsg render;
			if (!olive::ipc::RenderFrameMsg::from_json(message, &render)) {
				return write(error_message(
					QStringLiteral("invalid render_frame message")));
			}
			return render_frame(render);
		}

		if (type == QLatin1String(olive::ipc::msgtype::k_cancel)) {
			// Stage 5 wires cancellation into in-flight jobs. Stage 2 has only synchronous single-frame work.
			return true;
		}

		if (type == QLatin1String(olive::ipc::msgtype::k_shutdown)) {
			shutdown_requested_ = true;
			return true;
		}

		return write(
			error_message(QStringLiteral("unknown message type: %1").arg(type)));
	}

	bool shutdown_requested() const
	{
		return shutdown_requested_;
	}

private:
	bool write(const QJsonObject &message)
	{
		const bool ok = olive::ipc::write_message(out_, message);
		out_->flush();
		return ok;
	}

	bool attach_output_pool(const olive::ipc::HandshakeMsg &hs)
	{
		if (hs.protocol_version != k_protocol_version) {
			return write(
				error_message(QStringLiteral("unsupported protocol version %1")
								 .arg(hs.protocol_version)));
		}

		if (hs.shm_key.isEmpty() || hs.output_slots <= 0 ||
			hs.slot_data_bytes <= 0) {
			return write(error_message(QStringLiteral(
				"handshake missing output shared-memory geometry")));
		}

		const size_t bytes = olive::ipc::FrameSlotPool::bytes_needed(
			uint32_t(hs.output_slots), size_t(hs.slot_data_bytes));
		if (!output_region_.open(hs.shm_key, bytes,
								 olive::ipc::SharedMemoryRegion::k_attach)) {
			return write(error_message(
				QStringLiteral("failed to attach shared memory: %1")
					.arg(output_region_.error())));
		}

		output_pool_ = olive::ipc::FrameSlotPool::attach(output_region_.data());
		if (!output_pool_->is_valid()) {
			output_region_.close();
			output_pool_.reset();
			return write(error_message(QStringLiteral(
				"shared memory does not contain a frame slot pool")));
		}

		input_pool_.reset();
		input_region_.close();
		if (hs.input_slots > 0) {
			if (hs.input_shm_key.isEmpty() || hs.input_slot_data_bytes <= 0) {
				return write(error_message(QStringLiteral(
					"handshake missing input shared-memory geometry")));
			}

			const size_t input_bytes = olive::ipc::FrameSlotPool::bytes_needed(
				uint32_t(hs.input_slots), size_t(hs.input_slot_data_bytes));
			if (!input_region_.open(hs.input_shm_key, input_bytes,
									olive::ipc::SharedMemoryRegion::k_attach)) {
				return write(error_message(
					QStringLiteral("failed to attach input shared memory: %1")
						.arg(input_region_.error())));
			}

			input_pool_ =
				olive::ipc::FrameSlotPool::attach(input_region_.data());
			if (!input_pool_->is_valid()) {
				input_region_.close();
				input_pool_.reset();
				return write(error_message(QStringLiteral(
					"input shared memory does not contain a frame slot pool")));
			}
		}

		return true;
	}

	bool load_graph(const QString &path)
	{
		{
			QFileInfo fi(path);
			if (!fi.exists()) {
				log_error(
					QStringLiteral("LoadGraph: graph file does not exist: %1")
						.arg(path));
				return write(error_message(
					QStringLiteral("graph file does not exist: %1").arg(path)));
			}
			if (fi.size() == 0) {
				log_error(QStringLiteral("LoadGraph: graph file is empty: %1")
							 .arg(path));
				return write(error_message(
					QStringLiteral("graph file is empty: %1").arg(path)));
			}
			log_error(
				QStringLiteral("LoadGraph: loading %1 (%2 bytes, readable=%3)")
					.arg(path)
					.arg(fi.size())
					.arg(fi.isReadable()));
		}

		auto loaded = std::make_unique<olive::Project>();
		// Do not call Initialize() here: project serializers expect a blank
		// project (root_ == nullptr) and will set root themselves. Calling
		// Initialize() first triggers Q_ASSERT(!root_) in Project::Load.

		olive::ProjectSerializer::Result result =
			olive::ProjectSerializer::load(loaded.get(), path,
										   olive::ProjectSerializer::k_project);
		if (result != olive::ProjectSerializer::k_success) {
			return write(
				error_message(QStringLiteral("failed to load graph %1: %2")
								 .arg(path, result.get_details())));
		}

		project_ = std::move(loaded);
		node_by_token_.clear();
		color_processor_cache_.clear();

		const auto &data = result.get_load_data();
		for (auto it = data.node_ptrs.cbegin(); it != data.node_ptrs.cend();
			 ++it) {
			node_by_token_.insert(QString::number(it.key()), it.value());
		}
		for (auto it = data.node_uuids.cbegin(); it != data.node_uuids.cend();
			 ++it) {
			node_by_token_.insert(it.value().toString(), it.key());
			node_by_token_.insert(it.value().toString(QUuid::WithoutBraces),
								  it.key());
		}

		QJsonObject ack;
		ack["type"] = QStringLiteral("graph_loaded");
		ack["nodes"] = node_by_token_.size();
		return write(ack);
	}

	olive::Node *find_node(const QString &token) const
	{
		if (olive::Node *node = node_by_token_.value(token, nullptr)) {
			return node;
		}

		bool ok = false;
		const quintptr ptr = token.toULongLong(&ok, 0);
		if (ok) {
			return node_by_token_.value(QString::number(ptr), nullptr);
		}

		return nullptr;
	}

	bool render_frame(const olive::ipc::RenderFrameMsg &message)
	{
		if (!project_) {
			return write(error_message(
				QStringLiteral("render_frame received before load_graph"),
				message.ticket_id));
		}
		if (!output_pool_ || !output_pool_->is_valid()) {
			return write(error_message(
				QStringLiteral(
					"render_frame received before output shm handshake"),
				message.ticket_id));
		}

		olive::Node *node = find_node(message.node_uuid);
		if (!node) {
			return write(
				error_message(QStringLiteral("render node not found: %1")
								 .arg(message.node_uuid),
							 message.ticket_id));
		}

		QVector<int> input_slots;
		const QVector<int> requested_input_slots =
			message.input_slots.isEmpty() && message.input_slot >= 0 ?
				QVector<int>{ message.input_slot } :
				message.input_slots;
		if (!requested_input_slots.isEmpty()) {
			if (!input_pool_ || !input_pool_->is_valid()) {
				return write(error_message(
					QStringLiteral(
						"render_frame referenced input slot without input pool"),
					message.ticket_id));
			}

			for (int requested_slot : requested_input_slots) {
				if (requested_slot < 0 ||
					requested_slot >= int(input_pool_->slot_count())) {
					for (int slot : input_slots) {
						input_pool_->release(uint32_t(slot));
					}
					return write(error_message(
						QStringLiteral("input slot index out of range"),
						message.ticket_id));
				}

				uint32_t consumed_slot = 0;
				if (!input_pool_->consume(&consumed_slot)) {
					for (int slot : input_slots) {
						input_pool_->release(uint32_t(slot));
					}
					return write(
						error_message(QStringLiteral("input slot was not ready"),
									 message.ticket_id));
				}
				if (int(consumed_slot) != requested_slot) {
					input_pool_->release(consumed_slot);
					for (int slot : input_slots) {
						input_pool_->release(uint32_t(slot));
					}
					return write(error_message(
						QStringLiteral("input slot order mismatch"),
						message.ticket_id));
				}
				input_slots.append(int(consumed_slot));

				const olive::ipc::FrameSlotMeta *meta =
					input_pool_->meta(consumed_slot);
				if (meta) {
				}
			}
		}

		olive::VideoParams vparams(
			message.width > 0 ? message.width : k_default_width,
			message.height > 0 ? message.height : k_default_height,
			olive::Rational(1, k_default_frame_rate),
			message.format >= 0 ? olive::PixelFormat::Format(message.format) :
								  olive::PixelFormat::f32,
			message.channel_count > 0 ? message.channel_count :
										olive::VideoParams::k_rgba_channel_count);

		olive::RenderTicketPtr ticket = std::make_shared<olive::RenderTicket>();
		ticket->setProperty("node", olive::QtUtils::ptr_to_value(node));
		ticket->setProperty("time",
							QVariant::fromValue(olive::Rational(
								int(message.time_num), int(message.time_den))));
		ticket->setProperty("size", QSize(message.width, message.height));
		ticket->setProperty("matrix", QMatrix4x4());
		ticket->setProperty("format",
							message.format >= 0 ?
								olive::PixelFormat::Format(message.format) :
								olive::PixelFormat::invalid);
		ticket->setProperty("usecache", false);
		ticket->setProperty("channelcount", message.channel_count);
		ticket->setProperty("mode", olive::RenderMode::Mode(message.mode));
		ticket->setProperty("type", olive::RenderManager::k_type_video);
		ticket->setProperty("colormanager", olive::QtUtils::ptr_to_value(
												project_->color_manager()));

		{
			olive::ColorProcessorPtr color_output;
			if (message.has_color_transform) {
				QString cache_key = QStringLiteral("%1|%2|%3|%4")
										.arg(message.color_is_display ? 1 : 0)
										.arg(message.color_output,
											 message.color_view,
											 message.color_look);
				auto it = color_processor_cache_.find(cache_key);
				if (it != color_processor_cache_.end()) {
					color_output = it.value();
				} else {
					olive::ColorTransform transform;
					if (message.color_is_display) {
						transform = olive::ColorTransform(message.color_output,
														  message.color_view,
														  message.color_look);
					} else {
						transform = olive::ColorTransform(message.color_output);
					}
					color_output = olive::ColorProcessor::create(
						project_->color_manager(),
						project_->color_manager()->get_reference_color_space(),
						transform);
					if (color_output) {
						color_processor_cache_.insert(cache_key, color_output);
					}
				}
			}
			ticket->setProperty("coloroutput",
								QVariant::fromValue(color_output));
		}
		ticket->setProperty("vparam", QVariant::fromValue(vparams));
		ticket->setProperty("aparam",
							QVariant::fromValue(olive::AudioParams()));
		ticket->setProperty("return", olive::RenderManager::k_frame);
		ticket->setProperty("cache", QString());
		ticket->setProperty("cachetimebase",
							QVariant::fromValue(olive::Rational(1)));
		ticket->setProperty("cacheid", QVariant::fromValue(QUuid()));
		ticket->setProperty("multicam", olive::QtUtils::ptr_to_value(
											static_cast<void *>(nullptr)));
		ticket->setProperty(
			"ipc_input_pool",
			olive::QtUtils::ptr_to_value(input_pool_ ?
										   static_cast<void *>(&*input_pool_) :
										   static_cast<void *>(nullptr)));
		QVariantList input_slot_values;
		for (int slot : input_slots) {
			input_slot_values.append(slot);
		}
		ticket->setProperty("ipc_input_slots", input_slot_values);
		ticket->setProperty("ipc_input_slot_cursor", 0);
		ticket->setProperty("ipc_input_slot",
							input_slots.isEmpty() ? -1 : input_slots.front());

		ticket->start();
		olive::RenderProcessor::process(ticket, renderer_, nullptr,
										&shader_cache_);
		for (int slot : input_slots) {
			input_pool_->release(uint32_t(slot));
		}
		if (!ticket->has_result()) {
			return write(error_message(
				QStringLiteral("render produced no frame"), message.ticket_id));
		}

		olive::FramePtr frame = ticket->get().value<olive::FramePtr>();
		if (!frame || !frame->is_allocated()) {
			return write(error_message(QStringLiteral("render result was empty"),
									  message.ticket_id));
		}

		uint32_t slot = 0;
		if (!output_pool_->acquire(&slot)) {
			return write(
				error_message(QStringLiteral("no free output frame slot"),
							 message.ticket_id));
		}

		const int data_size = frame->linesize_bytes() * frame->height();
		if (data_size > int(output_pool_->slot_data_bytes())) {
			output_pool_->release(slot);
			log_error(QString("Output frame size") + QString::number(data_size));
			log_error(QString("Slot size") +
					 QString::number(output_pool_->slot_data_bytes()));
			return write(error_message(
				QStringLiteral("rendered frame does not fit output slot "),
				message.ticket_id));
		}

		std::memcpy(output_pool_->slot_data(slot), frame->const_data(),
					size_t(data_size));
		olive::ipc::FrameSlotMeta *meta = output_pool_->meta(slot);
		meta->id = message.ticket_id;
		meta->time_num = frame->timestamp().numerator();
		meta->time_den = frame->timestamp().denominator();
		meta->width = frame->width();
		meta->height = frame->height();
		meta->format = int32_t(frame->format());
		meta->channel_count = frame->channel_count();
		meta->linesize = frame->linesize_bytes();
		meta->data_size = data_size;

		if (!output_pool_->publish(slot)) {
			output_pool_->release(slot);
			return write(error_message(
				QStringLiteral("failed to publish output frame slot"),
				message.ticket_id));
		}
		olive::ipc::FrameReadyMsg ready;
		ready.ticket_id = message.ticket_id;
		ready.output_slot = int(slot);
		return write(ready.to_json());
	}

	olive::Renderer *renderer_;
	QFile *out_;
	bool shutdown_requested_ = false;
	std::unique_ptr<olive::Project> project_;
	QHash<QString, olive::Node *> node_by_token_;
	olive::ipc::SharedMemoryRegion output_region_;
	std::optional<olive::ipc::FrameSlotPool> output_pool_;
	olive::ipc::SharedMemoryRegion input_region_;
	std::optional<olive::ipc::FrameSlotPool> input_pool_;
	olive::ShaderCache shader_cache_;
	QHash<QString, olive::ColorProcessorPtr> color_processor_cache_;
};

} // namespace

int main(int argc, char *argv[])
{
	QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
	install_surface_format();

	QGuiApplication app(argc, argv);

#ifdef Q_OS_MACOS
	HideWorkerDockIcon();
#endif

	QCoreApplication::setOrganizationName(QStringLiteral("oakvideoeditor.org"));
	QCoreApplication::setApplicationName(QStringLiteral("oak-render-worker"));

	QString backend = QStringLiteral("opengl");
	const QStringList args = app.arguments();
	for (int i = 1; i < args.size(); ++i) {
		if (args[i] == QStringLiteral("--backend") && i + 1 < args.size()) {
			backend = args[i + 1].toLower();
			++i;
		}
	}

#ifdef Q_OS_LINUX
	std::signal(SIGSEGV, print_backtrace);
	std::signal(SIGABRT, print_backtrace);
	std::signal(SIGFPE, print_backtrace);
#endif

	QFile in;
	QFile out;
	if (!in.open(stdin, QIODevice::ReadOnly | QIODevice::Unbuffered) ||
		!out.open(stdout, QIODevice::WriteOnly | QIODevice::Unbuffered)) {
		log_error(QStringLiteral("failed to open stdio control pipes"));
		return 1;
	}

	olive::Renderer *renderer;
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	auto *dynamic_renderer = new olive::DynamicRenderer(backend);
	if (dynamic_renderer->init()) {
		dynamic_renderer->post_init();
		renderer = dynamic_renderer;
	} else {
		delete dynamic_renderer;
		qWarning() << "Failed to initialize dynamic" << backend
				   << "backend, falling back to direct OpenGL renderer";
		renderer = new olive::OpenGLRenderer();
		if (!renderer->init()) {
			log_error(QStringLiteral("failed to initialize OpenGL renderer"));
			delete renderer;
			return 1;
		}
		renderer->post_init();
	}
#else
	renderer = new olive::OpenGLRenderer();
	if (!renderer->Init()) {
		LogError(QStringLiteral("failed to initialize OpenGL renderer"));
		delete renderer;
		return 1;
	}
	renderer->PostInit();
#endif

	// Validate the renderer. For OpenGL we check the GL context; for Vulkan we
	// rely on Init()/PostInit() succeeding (there is no QOpenGLContext).
	bool renderer_valid = true;
	QOpenGLContext *ctx = nullptr;
	if (backend == QStringLiteral("opengl")) {
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		if (auto *loaded_renderer =
				dynamic_cast<olive::DynamicRenderer *>(renderer)) {
			ctx = loaded_renderer->open_gl_context();
		} else
#endif
		{
			ctx = static_cast<olive::OpenGLRenderer *>(renderer)->context();
		}
		if (!ctx || !ctx->isValid()) {
			renderer_valid = false;
		}
	}
	if (!renderer_valid) {
		log_error(QStringLiteral("OpenGL context is not valid after init"));
		renderer->destroy();
		renderer->post_destroy();
		delete renderer;
		return 1;
	}

	int exit_code = 0;
	{
		RenderWorker worker(renderer, &out);
		if (!worker.initialize_runtime() || !worker.send_startup_handshake()) {
			exit_code = 1;
		} else {
			QByteArray buffer;
			while (!worker.shutdown_requested() && !in.atEnd()) {
				const QByteArray chunk = in.readLine();
				if (chunk.isEmpty()) {
					break;
				}

				buffer.append(chunk);
				while (true) {
					QJsonObject message;
					bool ok = true;
					if (!olive::ipc::read_message(&buffer, &message, &ok)) {
						if (!ok) {
							olive::ipc::write_message(
								&out, error_message(QStringLiteral(
										  "malformed control message")));
							out.flush();
							continue;
						}
						break;
					}

					if (!worker.handle(message)) {
						exit_code = 1;
						break;
					}
				}
			}
		}
	}

	renderer->destroy();
	renderer->post_destroy();
	delete renderer;

	return exit_code;
}
