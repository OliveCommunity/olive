#include <gtest/gtest.h>

#include "node/traverser.h"
#include "node/value.h"
#include "render/job/pluginjob.h"
#include "render/texture.h"
#include "render/videoparams.h"

namespace
{

class PluginJobTraverser : public olive::NodeTraverser {
public:
	void resolve(olive::NodeValue &value)
	{
		resolve_jobs(value);
	}

	bool called() const
	{
		return called_;
	}

protected:
	olive::TexturePtr process_plugin_job(olive::TexturePtr /*texture*/,
									   olive::TexturePtr destination,
									   const olive::Node * /*node*/) override
	{
		called_ = true;
		return destination;
	}

private:
	bool called_ = false;
};

} // namespace

TEST(PluginRenderPipeline, PluginJobIsResolved)
{
	olive::VideoParams params(320, 240, olive::core::PixelFormat::u8, 4);

	olive::plugin::PluginJob job(nullptr, nullptr, olive::NodeValueRow());
	olive::TexturePtr job_tex = olive::Texture::job(params, job);

	olive::NodeValue val(olive::NodeValue::k_texture, job_tex);

	PluginJobTraverser traverser;
	traverser.set_cache_video_params(params);
	traverser.resolve(val);

	EXPECT_TRUE(traverser.called());
	ASSERT_TRUE(val.to_texture());
	EXPECT_NE(val.to_texture().get(), job_tex.get());
}
