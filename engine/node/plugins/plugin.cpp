/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "plugin.h"

#include "render/rendermanager.h"
#include "render/job/pluginjob.h"
#include "pluginSupport/oliveplugininstance.h"
#include "common/current.h"

#include <algorithm>
#include <cstring>
#include <olive/core/core.h>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <QVector2D>
#include <QVector3D>

namespace
{
QHash<QString, QHash<QString, QVariant>> g_plugin_param_defaults;

static bool is_normalised_coord_system(const OFX::Host::Param::Base *param)
{
	return param->getDefaultCoordinateSystem() ==
		   kOfxParamCoordinatesNormalised;
}

static void get_project_extent(double &x_size, double &y_size)
{
	auto &vp = Current::getInstance().current_video_params();
	x_size = vp.width() * vp.pixel_aspect_ratio().to_double();
	y_size = vp.height();
}

static double to_canonical(double normalised, double extent)
{
	return extent > 0 ? normalised * extent : normalised;
}

QVariant default_value_for_param(const OFX::Host::Param::Base *param)
{
	if (!param) {
		return QVariant();
	}
	const std::string &ofx_type = param->getType();
	const auto &props = param->getProperties();

	if (ofx_type == kOfxParamTypeInteger || ofx_type == kOfxParamTypeChoice) {
		return props.getIntProperty(kOfxParamPropDefault);
	}
	if (ofx_type == kOfxParamTypeBoolean) {
		return props.getIntProperty(kOfxParamPropDefault) != 0;
	}
	if (ofx_type == kOfxParamTypeDouble) {
		double val = props.getDoubleProperty(kOfxParamPropDefault);
		if (is_normalised_coord_system(param)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			val = to_canonical(val, x_size);
		}
		return val;
	}
	if (ofx_type == kOfxParamTypeString || ofx_type == kOfxParamTypeStrChoice ||
		ofx_type == kOfxParamTypeCustom) {
		return QString::fromStdString(
			props.getStringProperty(kOfxParamPropDefault));
	}
	if (ofx_type == kOfxParamTypeRGB || ofx_type == kOfxParamTypeRGBA) {
		const int count = (ofx_type == kOfxParamTypeRGBA) ? 4 : 3;
		double values[4] = { 0.0, 0.0, 0.0, 1.0 };
		props.getDoublePropertyN(kOfxParamPropDefault, values, count);
		const double alpha = (count == 4) ? values[3] : 1.0;
		return QVariant::fromValue(
			olive::core::Color(values[0], values[1], values[2], alpha));
	}
	if (ofx_type == kOfxParamTypeDouble2D || ofx_type == kOfxParamTypeDouble3D ||
		ofx_type == kOfxParamTypeInteger2D ||
		ofx_type == kOfxParamTypeInteger3D) {
		const bool is_double = (ofx_type == kOfxParamTypeDouble2D ||
								ofx_type == kOfxParamTypeDouble3D);
		const int count = (ofx_type == kOfxParamTypeDouble2D ||
						   ofx_type == kOfxParamTypeInteger2D) ?
							  2 :
							  3;
		if (is_double) {
			double values[3] = { 0.0, 0.0, 0.0 };
			props.getDoublePropertyN(kOfxParamPropDefault, values, count);
			if (is_normalised_coord_system(param)) {
				double x_size, y_size;
				get_project_extent(x_size, y_size);
				values[0] = to_canonical(values[0], x_size);
				values[1] = to_canonical(values[1], y_size);
				if (count == 3) {
					values[2] = to_canonical(values[2], x_size);
				}
			}
			if (count == 2) {
				return QVector2D(values[0], values[1]);
			}
			return QVector3D(values[0], values[1], values[2]);
		}
		int values[3] = { 0, 0, 0 };
		props.getIntPropertyN(kOfxParamPropDefault, values, count);
		if (count == 2) {
			return QVector2D(values[0], values[1]);
		}
		return QVector3D(values[0], values[1], values[2]);
	}
	if (ofx_type == kOfxParamTypeBytes) {
		return QByteArray();
	}

	return QVariant();
}

/**
 * @brief Deduce whether an RGB/RGBA parameter semantically represents a color
 *        pickers or per-channel scalar values (e.g. gamma, contrast).
 *
 * Uses heuristics based on label, hint, display range, default values,
 * and parent group name.
 */
QString deduce_color_semantic(const OFX::Host::Param::Base *param,
							const QHash<QString, QString> &group_labels)
{
	const std::string &ofx_type = param->getType();
	if (ofx_type != kOfxParamTypeRGB && ofx_type != kOfxParamTypeRGBA) {
		return QStringLiteral("color");
	}

	const QString label = QString::fromStdString(param->getLabel()).toLower();
	const QString hint = QString::fromStdString(param->getHint()).toLower();
	const QString name = QString::fromStdString(param->getName()).toLower();

	// Rule 1: explicit color keywords → color
	static const QStringList k_color_keywords = {
		QStringLiteral("color"), QStringLiteral("colour"),
		QStringLiteral("fill"), QStringLiteral("tint"), QStringLiteral("key")
	};
	for (const QString &kw : k_color_keywords) {
		if (label.contains(kw) || hint.contains(kw) || name.contains(kw)) {
			return QStringLiteral("color");
		}
	}

	// Rule 2: explicit scalar/adjustment keywords → scalar
	static const QStringList k_scalar_keywords = {
		QStringLiteral("gamma"),	  QStringLiteral("contrast"),
		QStringLiteral("gain"),		  QStringLiteral("offset"),
		QStringLiteral("saturation"), QStringLiteral("exposure"),
		QStringLiteral("brightness"), QStringLiteral("lift"),
		QStringLiteral("multiply"),	  QStringLiteral("scale"),
		QStringLiteral("pivot")
	};
	for (const QString &kw : k_scalar_keywords) {
		if (label.contains(kw) || hint.contains(kw) || name.contains(kw)) {
			return QStringLiteral("scalar");
		}
	}

	// Rule 3: display range significantly outside/asymmetric to [0,1] → scalar
	const auto &props = param->getProperties();
	const int dim = (ofx_type == kOfxParamTypeRGBA) ? 4 : 3;
	double dmin[4] = { 0, 0, 0, 0 };
	double dmax[4] = { 1, 1, 1, 1 };
	props.getDoublePropertyN(kOfxParamPropDisplayMin, dmin, dim);
	props.getDoublePropertyN(kOfxParamPropDisplayMax, dmax, dim);
	bool range_looks_scalar = false;
	for (int i = 0; i < dim; ++i) {
		if (dmin[i] < -0.01 || dmax[i] > 1.01) {
			range_looks_scalar = true;
			break;
		}
	}
	if (range_looks_scalar) {
		return QStringLiteral("scalar");
	}

	// Rule 4: default values all equal → scalar (lean)
	double defs[4] = { 0, 0, 0, 1 };
	props.getDoublePropertyN(kOfxParamPropDefault, defs, dim);
	bool all_equal = true;
	for (int i = 1; i < dim; ++i) {
		if (defs[i] != defs[0]) {
			all_equal = false;
			break;
		}
	}
	if (all_equal) {
		return QStringLiteral("scalar");
	}

	// Rule 5: parent group contains scalar keywords → scalar
	const QString parent =
		QString::fromStdString(param->getParentName()).toLower();
	if (!parent.isEmpty()) {
		for (const QString &kw : k_scalar_keywords) {
			if (parent.contains(kw)) {
				return QStringLiteral("scalar");
			}
		}
	}

	// Fallback
	return QStringLiteral("color");
}

QHash<QString, QVariant> build_default_values(
	const std::map<std::string, OFX::Host::Param::Instance *> &params)
{
	QHash<QString, QVariant> defaults;
	for (const auto &param : params) {
		const std::string &ofx_type = param.second->getType();
		if (ofx_type == kOfxParamTypeGroup || ofx_type == kOfxParamTypePage ||
			ofx_type == kOfxParamTypePushButton) {
			continue;
		}
		const auto &props = param.second->getProperties();
		bool is_secret = props.getIntProperty(kOfxParamPropSecret) != 0;
		const QString input_id =
			QString::fromStdString(param.second->getName());
		if (input_id.isEmpty()) {
			continue;
		}
		QVariant default_value = default_value_for_param(param.second);
		if (!default_value.isValid()) {
			continue;
		}
		defaults.insert(input_id, default_value);
	}
	return defaults;
}
}
static QString
clip_label_for_name(const std::string &name,
				 const OFX::Host::ImageEffect::ClipDescriptor *desc)
{
	if (name == kOfxImageEffectSimpleSourceClipName) {
		return olive::plugin::PluginNode::tr("Source");
	}
	if (name == kOfxImageEffectTransitionSourceFromClipName) {
		return olive::plugin::PluginNode::tr("From");
	}
	if (name == kOfxImageEffectTransitionSourceToClipName) {
		return olive::plugin::PluginNode::tr("To");
	}

	if (desc) {
		const std::string &label =
			desc->getProps().getStringProperty(kOfxPropLabel);
		if (!label.empty()) {
			return QString::fromStdString(label);
		}
	}

	return QString::fromStdString(name);
}

olive::plugin::PluginNode::PluginNode(OFX::Host::ImageEffect::Instance *plugin)
{
	plugin_instance_ = plugin;

	const std::string &ctx = plugin_instance_->getContext();
	if (ctx == kOfxImageEffectContextFilter) {
		sub_category_ = tr("Filter");
	} else if (ctx == kOfxImageEffectContextGenerator) {
		sub_category_ = tr("Generator");
	} else if (ctx == kOfxImageEffectContextTransition) {
		sub_category_ = tr("Transition");
	} else {
		sub_category_ = tr("General");
	}

	bool has_texture_input = false;
	QHash<QString, QString> group_labels;
	QHash<QString, QString> page_labels;
	QHash<QString, QString> page_for_param;

	auto params = plugin_instance_->getParams();
	const QString plugin_id =
		QString::fromStdString(plugin_instance_->getPlugin()->getIdentifier());
	auto defaults_iter = g_plugin_param_defaults.find(plugin_id);
	if (defaults_iter == g_plugin_param_defaults.end()) {
		g_plugin_param_defaults.insert(plugin_id, build_default_values(params));
		defaults_iter = g_plugin_param_defaults.find(plugin_id);
	}
	const QHash<QString, QVariant> &defaults = defaults_iter.value();
	for (auto param : params) {
		const std::string &ofx_type = param.second->getType();
		if (ofx_type == kOfxParamTypeGroup) {
			const QString name = QString::fromStdString(param.first);
			const QString label =
				QString::fromStdString(param.second->getLabel());
			group_labels.insert(name, label.isEmpty() ? name : label);
		} else if (ofx_type == kOfxParamTypePage) {
			const QString name = QString::fromStdString(param.first);
			const QString label =
				QString::fromStdString(param.second->getLabel());
			page_labels.insert(name, label.isEmpty() ? name : label);

			const auto &props = param.second->getProperties();
			int count = props.getDimension(kOfxParamPropPageChild);
			for (int i = 0; i < count; ++i) {
				const std::string &child =
					props.getStringProperty(kOfxParamPropPageChild, i);
				if (child == kOfxParamPageSkipRow ||
					child == kOfxParamPageSkipColumn) {
					continue;
				}
				page_for_param.insert(QString::fromStdString(child),
									  page_labels.value(name));
			}
		}
	}

	for (auto param : params) {
		NodeValue::Type type = NodeValue::k_none;

		const std::string &ofx_type = param.second->getType();
		if (ofx_type == kOfxParamTypeInteger) {
			type = NodeValue::k_int;
		} else if (ofx_type == kOfxParamTypeDouble) {
			type = NodeValue::k_float;
		} else if (ofx_type == kOfxParamTypeBoolean) {
			type = NodeValue::k_boolean;
		} else if (ofx_type == kOfxParamTypeString) {
			type = NodeValue::k_text;
		} else if (ofx_type == kOfxParamTypeRGB ||
				   ofx_type == kOfxParamTypeRGBA) {
			type = NodeValue::k_color;
		} else if (ofx_type == kOfxParamTypeChoice) {
			type = NodeValue::k_combo;
		} else if (ofx_type == kOfxParamTypeDouble2D ||
				   ofx_type == kOfxParamTypeInteger2D) {
			type = NodeValue::k_vec2;
		} else if (ofx_type == kOfxParamTypeDouble3D ||
				   ofx_type == kOfxParamTypeInteger3D) {
			type = NodeValue::k_vec3;
		} else if (ofx_type == kOfxParamTypeStrChoice) {
			type = NodeValue::k_str_combo;
		} else if (ofx_type == kOfxParamTypeBytes ||
				   ofx_type == kOfxParamTypeCustom) {
			type = NodeValue::k_binary;
		} else if (ofx_type == kOfxParamTypePushButton) {
			type = NodeValue::k_push_button;
		} else if (ofx_type == kOfxParamTypeGroup ||
				   ofx_type == kOfxParamTypePage) {
			continue;
		} else {
			type = NodeValue::k_none;
		}

		const QString input_id =
			QString::fromStdString(param.second->getName());
		if (input_id.isEmpty()) {
			continue;
		}
		const auto &props = param.second->getProperties();
		bool is_secret = props.getIntProperty(kOfxParamPropSecret) != 0;
		if (type == NodeValue::k_none) {
			continue;
		}
		QVariant default_value = defaults.value(input_id, QVariant());
		if (default_value.isValid()) {
			add_input(input_id, type, default_value);
			if (type != NodeValue::k_push_button) {
				set_standard_value(input_id, default_value);
			}
		} else {
			add_input(input_id, type);
		}
		if (is_secret) {
			set_input_flag(input_id, k_input_flag_hidden);
		}
		const QString label = QString::fromStdString(param.second->getLabel());
		if (!label.isEmpty()) {
			set_input_name(input_id, label);
		} else {
			set_input_name(input_id, input_id);
		}
		const QString parent =
			QString::fromStdString(param.second->getParentName());
		if (!parent.isEmpty()) {
			set_input_property(input_id, QStringLiteral("ui_group"),
							 group_labels.value(parent, parent));
		}
		if (page_for_param.contains(input_id)) {
			set_input_property(input_id, QStringLiteral("ui_page"),
							 page_for_param.value(input_id));
		}
		if (type == NodeValue::k_color) {
			QString semantic = deduce_color_semantic(param.second, group_labels);
			set_input_property(input_id, QStringLiteral("color_semantic"),
							 semantic);

			const int dim = (ofx_type == kOfxParamTypeRGBA) ? 4 : 3;
			double dmin[4] = { 0, 0, 0, 0 };
			double dmax[4] = { 1, 1, 1, 1 };
			props.getDoublePropertyN(kOfxParamPropDisplayMin, dmin, dim);
			props.getDoublePropertyN(kOfxParamPropDisplayMax, dmax, dim);
			set_input_property(input_id, QStringLiteral("min"), dmin[0]);
			set_input_property(input_id, QStringLiteral("max"), dmax[0]);

			const QString hint =
				QString::fromStdString(param.second->getHint());
			if (!hint.isEmpty()) {
				set_input_property(input_id, QStringLiteral("tooltip"), hint);
			}
		}
		if (type == NodeValue::k_combo || type == NodeValue::k_str_combo) {
			QStringList option_labels;
			QStringList option_values;
			const int label_count =
				props.getDimension(kOfxParamPropChoiceOption);
			const int value_count = props.getDimension(kOfxParamPropChoiceEnum);

			for (int i = 0; i < label_count; ++i) {
				const std::string &label =
					props.getStringProperty(kOfxParamPropChoiceOption, i);
				option_labels.append(QString::fromStdString(label));
			}

			for (int i = 0; i < value_count; ++i) {
				const std::string &value =
					props.getStringProperty(kOfxParamPropChoiceEnum, i);
				option_values.append(QString::fromStdString(value));
			}

			if (option_labels.isEmpty() && !option_values.isEmpty()) {
				option_labels = option_values;
			}
			if (option_values.isEmpty() && !option_labels.isEmpty()) {
				option_values = option_labels;
			}

			const int order_count =
				props.getDimension(kOfxParamPropChoiceOrder);
			if (order_count == option_labels.size() &&
				option_labels.size() == option_values.size()) {
				QVector<int> indices(option_labels.size());
				for (int i = 0; i < indices.size(); ++i) {
					indices[i] = i;
				}

				std::stable_sort(
					indices.begin(), indices.end(), [&](int a, int b) {
						return props.getIntProperty(kOfxParamPropChoiceOrder,
													a) <
							   props.getIntProperty(kOfxParamPropChoiceOrder,
													b);
					});

				QStringList ordered_labels;
				QStringList ordered_values;
				for (int index : indices) {
					ordered_labels.append(option_labels.at(index));
					ordered_values.append(option_values.at(index));
				}
				option_labels = ordered_labels;
				option_values = ordered_values;
			}

			if (!option_labels.isEmpty()) {
				set_combo_box_strings(input_id, option_labels);
				if (type == NodeValue::k_str_combo) {
					set_input_property(input_id,
									 QStringLiteral("combo_value_str"),
									 option_values);
				}
			}
		}
	}

	const auto &clips = plugin_instance_->getDescriptor().getClips();
	for (const auto &entry : clips) {
		if (entry.first == kOfxImageEffectOutputClipName) {
			continue;
		}
		QString input_id = QString::fromStdString(entry.first);
		add_input(input_id, NodeValue::k_texture);
		set_input_name(input_id, clip_label_for_name(entry.first, entry.second));
		has_texture_input = true;
	}

	const QString source_id =
		QString::fromUtf8(kOfxImageEffectSimpleSourceClipName);
	if (has_input_with_id(source_id)) {
		set_effect_input(source_id);
	} else if (has_input_with_id(k_texture_input)) {
		set_effect_input(k_texture_input);
	} else {
		if (has_texture_input) {
			add_input(k_texture_input, NodeValue::k_texture);
			set_input_name(k_texture_input, tr("Texture"));
			set_effect_input(k_texture_input);
		}
	}
}

olive::plugin::PluginNode::~PluginNode() = default;
QString olive::plugin::PluginNode::name() const
{
	const auto *plugin = plugin_instance_->getPlugin();
	return plugin->getDescriptor()
		.getProps()
		.getStringProperty(kOfxPropLabel)
		.data();
}

QVector<olive::Node::CategoryID> olive::plugin::PluginNode::category() const
{
	return { olive::Node::k_category_open_fx };
}

QString olive::plugin::PluginNode::sub_category() const
{
	return sub_category_;
}

QString olive::plugin::PluginNode::description() const
{
	const auto *plugin = plugin_instance_->getPlugin();
	return plugin->getDescriptor()
		.getProps()
		.getStringProperty(kOfxPropPluginDescription)
		.data();
}
void olive::plugin::PluginNode::process_samples(const NodeValueRow &values,
											   const SampleBuffer &input,
											   SampleBuffer &output,
											   int index) const
{
	Q_UNUSED(values)
	Q_UNUSED(index)

	if (!input.is_allocated() || input.channel_count() == 0 ||
		input.sample_count() == 0) {
		if (output.is_allocated()) {
			output.silence();
		}
		return;
	}

	if (!output.is_allocated() ||
		output.channel_count() != input.channel_count() ||
		output.sample_count() != input.sample_count()) {
		output.set_audio_params(input.audio_params());
		output.set_sample_count(input.sample_count());
		output.allocate();
	}

	if (!output.is_allocated()) {
		return;
	}

	for (int channel = 0; channel < input.channel_count(); ++channel) {
		output.fast_set(input, channel);
	}
}

void olive::plugin::PluginNode::generate_frame(FramePtr frame,
											  const GenerateJob &job) const
{
	Q_UNUSED(job)

	if (!frame) {
		return;
	}

	if (!frame->is_allocated()) {
		frame->allocate();
	}

	if (!frame->is_allocated()) {
		return;
	}

	std::memset(frame->data(), 0, static_cast<size_t>(frame->allocated_size()));
}
void olive::plugin::PluginNode::value(const NodeValueRow &value,
									  const NodeGlobals &globals,
									  NodeValueTable *table) const
{
	for (auto it = value.cbegin(); it != value.cend(); ++it) {
		const NodeValue &input_value = it.value();
		if (input_value.type() == NodeValue::k_texture ||
			input_value.type() == NodeValue::k_none) {
			continue;
		}
		NodeValue tagged = input_value;
		tagged.set_tag(it.key());
		table->push(tagged);
	}

	TexturePtr tex = nullptr;
	const QString source_key =
		QString::fromUtf8(kOfxImageEffectSimpleSourceClipName);
	if (value.contains(source_key)) {
		tex = value.value(source_key).to_texture();
	}
	if (!tex) {
		tex = value.value(k_texture_input).to_texture();
	}
	if (!tex) {
		for (auto it = value.cbegin(); it != value.cend(); ++it) {
			if (it.value().type() == NodeValue::k_texture) {
				tex = it.value().to_texture();
				if (tex) {
					break;
				}
			}
		}
	}
	if (tex && plugin_instance_) {
		PluginJob job(plugin_instance_, this, value, globals.time().in());

		table->push(NodeValue::k_texture, tex->to_job(job), this);
	}
}
void olive::plugin::PluginNode::push_button_clicked(QString name)
{
}

QString olive::plugin::PluginNode::id() const
{
	const auto *plugin = plugin_instance_->getPlugin();
	return plugin->getIdentifier().data();
}

olive::Node *olive::plugin::PluginNode::copy() const
{
	if (!plugin_instance_) {
		return nullptr;
	}

	const auto &contexts = plugin_instance_->getPlugin()->getContexts();
	std::string context = kOfxImageEffectContextFilter;
	if (!contexts.empty() &&
		contexts.find(kOfxImageEffectContextFilter) == contexts.end()) {
		context = *contexts.begin();
	}

	auto *instance =
		plugin_instance_->getPlugin()->createInstance(context, nullptr);
	if (!instance) {
		return nullptr;
	}

	auto *node = new PluginNode(instance);
	if (auto *olive_instance = dynamic_cast<OlivePluginInstance *>(instance)) {
		olive_instance->setNode(
			std::shared_ptr<PluginNode>(node, [](PluginNode *) {}));
	}
	return node;
}
