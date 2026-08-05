#pragma once
#include <string>
namespace olive {
class ProxyManager {
public:
	enum ProxyState { k_proxy_missing, k_proxy_generating, k_proxy_ready };
	struct ProxyParams {
		int width = 0;
		int height = 0;
		int divider = 1;
		int crf = 0;
		std::string preset;
		std::string extension;
		bool include_audio = false;
	};
	static ProxyManager *instance() { static ProxyManager p; return &p; }
	static std::string proxy_state_to_string(ProxyState) { return std::string(); }
	static ProxyState proxy_state_from_string(const std::string &) { return k_proxy_missing; }
	static ProxyState get_proxy_state(const std::string &) { return k_proxy_missing; }
	static bool proxy_filename_has_audio(const std::string &) { return false; }
	static ProxyParams proxy_params_from_config() { return ProxyParams(); }
};
}
