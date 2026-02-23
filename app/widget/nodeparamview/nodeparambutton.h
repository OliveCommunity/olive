/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef NODEPARAMBUTTON_H
#define NODEPARAMBUTTON_H
#include "node/plugins/Plugin.h"

#include <QPushButton>

class NodeParamButton : public QPushButton{
Q_OBJECT
public:
	NodeParamButton(QString name, QWidget *parent = nullptr):QPushButton(parent)
	{
		this->name_=name;
		connect(this, &QPushButton::clicked, this, &NodeParamButton::pressed);
	}
signals:
	void onPressed(QString name);
private slots:
	void pressed(){
		emit onPressed(name_);
	}
private:
	QString name_;

};



#endif //NODEPARAMBUTTON_H
