#pragma once
#include <string>
#include <vector>
#include "render/job/acceleratedjob.h"
namespace olive {
class ShaderJob : public AcceleratedJob {
public:
	ShaderJob() = default;
	ShaderJob(const NodeValueRow &row) { (void) row; }
	NodeValue get(const std::string &id) const { (void) id; return NodeValue(); }
	void set_shader_id(const std::string &id) { (void) id; }
	void set_iterations(int iterations, const std::string &iterative_input)
	{
		(void) iterations; (void) iterative_input;
	}
	void set_interpolation(const std::string &id, int mode)
	{
		(void) id; (void) mode;
	}
	void set_vertex_coordinates(const std::vector<float> &coords)
	{
		(void) coords;
	}
};
}
