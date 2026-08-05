#pragma once
#include "render/job/acceleratedjob.h"
#include "render/colorprocessor.h"
namespace olive {
class Node;
class ColorTransformJob : public AcceleratedJob {
public:
	ColorTransformJob() = default;
	ColorTransformJob(const NodeValueRow &) {}
	NodeValue get_input_texture() const { return NodeValue(); }
	void set_input_texture(const NodeValue &) {}
	void set_color_processor(ColorProcessorPtr) {}
	void set_needs_custom_shader(const Node *) {}
	void set_function_name(const std::string &) {}
};
}
