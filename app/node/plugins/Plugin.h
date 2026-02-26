/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef PLUGIN_NODES_H
#define PLUGIN_NODES_H
#include "ofxhImageEffectAPI.h"
#include "ofxhPluginCache.h"
#include "ofxImageEffect.h"
#include "node/node.h"

namespace olive
{
namespace plugin
{
const QString kTextureInput = QStringLiteral("tex_in");

class PluginNode : public olive::Node{
public:
	PluginNode(OFX::Host::ImageEffect::Instance* plugin)	;
	~PluginNode() override;


	QString Name() const override;
	QString id() const override;
	QVector<CategoryID> Category() const override;
	QString Description() const override;

	void AddPushButton();
	void AddPage();

	Node *copy() const override;
	/**
   * @brief The main processing function
   *
   * The node's main purpose is to take values from inputs to set values in outputs. For whatever subclass node you
   * create, this is where the code for that goes.
   *
   * Note that as a video editor, the node graph has to work across time. Depending on the purpose of your node, it may
   * output different values depending on the time, and even if not, it will likely be receiving different input
   * depending on the time. Most of the difficult work here is handled by NodeInput::get_value() which you should pass
   * the `time` parameter to. It will return its value (at that time, if it's keyframed), or pass the time to a
   * corresponding output if it's connected to one. If your node doesn't directly deal with time, the default behavior
   * of the NodeParam objects will handle everything related to it automatically.
   */
	void Value(const NodeValueRow &value,
		const NodeGlobals &globals, NodeValueTable *table) const override;

	/**
   * @brief If Value() pushes a ShaderJob, this is the function that will process them.
   */
	virtual void ProcessSamples(const NodeValueRow &values,
								const SampleBuffer &input, SampleBuffer &output,
								int index) const override;

	/**
   * @brief If Value() pushes a GenerateJob, override this function for the image to create
   *
   * @param frame
   *
   * The destination buffer. It will already be allocated and ready for writing to.
   */
	virtual void GenerateFrame(FramePtr frame,
							   const GenerateJob &job) const override;


	/**
	 * @brief Called when an input value changes
	 */
	void InputValueChangedEvent(const QString &input, int element) override;
public slots:
	void pushButtonClicked(QString name);

};

}
}


#endif //PLUGIN_H
