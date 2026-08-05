#pragma once
#include <memory>
#include <string>
#include <vector>
#include "loopmode.h"
#include "render/cancelatom.h"
#include "project/footage/footagedescription.h"
namespace olive {
class Decoder;
using DecoderPtr = std::shared_ptr<Decoder>;
class Decoder {
public:
	virtual ~Decoder() = default;
	static std::vector<DecoderPtr> receive_list_of_all_decoders() { return {}; }
	FootageDescription probe(const std::string &, CancelAtom *) { return FootageDescription(); }
};
}
