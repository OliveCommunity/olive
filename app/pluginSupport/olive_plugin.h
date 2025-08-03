//
// Created by katherinesolar on 25-8-3.
//

#ifndef OLIVE_PLUGIN_H
#define OLIVE_PLUGIN_H
#include <cstdint>
struct olive_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};
struct olive_node_ctx_functions {
	bool (*get_ctx_int)(uint64_t handle, char *name, int *val);
	bool (*get_ctx_float)(uint64_t handle, char *name, float *out_val);
	bool (*get_ctx_rational)(uint64_t handle, char *name, double *out_val);
	bool (*get_ctx_bool)(uint64_t handle, char *name, bool *out_val);
	bool (*get_ctx_string)(uint64_t handle, char *name, char **out_str);
	bool (*get_ctx_font)(uint64_t handle, char *name, char **out_font);
	bool (*get_ctx_path)(uint64_t handle, char *name, char **out_path);
	bool (*get_sample_buffer_handle)(uint64_t handle, char *name, uint64_t *out_handle);
	bool (*get_ctx_samples)(uint64_t handle, char *name, uint64_t *out_handle);
	bool (*get_ctx_video_params)(uint64_t handle, char *name, uint64_t *out_params);
	bool (*get_ctx_audio_params)(uint64_t handle, char *name, uint64_t *out_params);
	bool (*get_ctx_subtitle_params)(uint64_t handle, char *name, uint64_t *out_params);
	bool (*get_ctx_bezier)(uint64_t handle, char *name, uint64_t *out_bezier);
	bool (*get_ctx_vec2)(uint64_t handle, char *name, double **vec);
	bool (*get_ctx_vec3)(uint64_t handle, char *name, double **vec);
	bool (*get_ctx_vec4)(uint64_t handle, char *name, double **vec);

	int64_t (*get_size_need)(char *name);
	// 下面几个函数请先获取缓冲区大小
	bool (*get_ctx_matrix)(uint64_t handle, char *name, int ***buffer, size_t buffer_size,
		int *out_rows, int *out_cols);
	bool (*get_ctx_texture)(uint64_t handle, char *name, uint8_t **buffer, size_t buffer_size,
		int *out_len);
	bool (*get_ctx_binary)(uint64_t handle, char *name, uint8_t* buffer, size_t buffer_size);
};
struct olive_node_methods {
	bool (*name)(char *buffer, size_t buffer_size); //最长63个char
	bool (*id)(char *buffer, size_t buffer_size); //最长63个char
	bool (*category)(char *buffer, size_t buffer_size); //最长63个char
	bool (*description)(char *buffer, size_t buffer_size); //最长1023个char
	void (*value)(struct olive_node_ctx_functions *functions);
};
#endif //OLIVE_PLUGIN_H
