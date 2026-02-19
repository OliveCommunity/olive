/*
  This file is part of Oak Video Editor - A fork of original project Olive 

*/

#include "multicampanel.h"

namespace olive
{

#define super TimeBasedPanel

MulticamPanel::MulticamPanel()
	: super(QStringLiteral("MultiCamPanel"))
{
	SetTimeBasedWidget(new MulticamWidget(this));

	Retranslate();
}

void MulticamPanel::Retranslate()
{
	super::Retranslate();

	SetTitle(tr("Multi-Cam"));
}

}
