/*
	This file is part of Oak Video Editor - A fork of original project Olive 

*/

#ifndef HUMANSTRINGS_H
#define HUMANSTRINGS_H

#include <olive/core/core.h>
#include <QObject>

namespace olive
{

using namespace core;

class HumanStrings : public QObject {
	Q_OBJECT
public:
	HumanStrings() = default;

	static QString SampleRateToString(const int &sample_rate);

	static QString ChannelLayoutToString(const uint64_t &layout);

	static QString FormatToString(const SampleFormat &f);
};

}

#endif // HUMANSTRINGS_H
