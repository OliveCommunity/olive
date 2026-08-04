/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Copyright OpenFX and contributors to the OpenFX project.
#ifndef OAK_PARAM_INSTANCE_H
#define OAK_PARAM_INSTANCE_H

#include "olive/core/util/rational.h"
#include "pluginSupport/oliveplugininstance.h"
#include <QString>
#include <QVector2D>
#include <QVector3D>
#include <QVariant>
#include "ofxhParam.h"
#include "node/nodeundo.h"
#include "node/plugins/plugin.h"
#include "undo/undocommand.h"
#include "common/current.h"
#include <iostream>
#include <mutex>
#include <qlogging.h>
namespace olive
{
namespace plugin
{

inline bool
is_normalised_coordinate_system(const OFX::Host::Param::Descriptor &descriptor)
{
	return descriptor.getDefaultCoordinateSystem() ==
		   kOfxParamCoordinatesNormalised;
}

inline void get_project_extent(double &x_size, double &y_size)
{
	auto &vp = Current::getInstance().current_video_params();
	x_size = vp.width() * vp.pixel_aspect_ratio().to_double();
	y_size = vp.height();
}

inline double to_normalised(double canonical, double extent)
{
	return extent > 0 ? canonical / extent : canonical;
}

inline double to_canonical(double normalised, double extent)
{
	return extent > 0 ? normalised * extent : normalised;
}

inline QString param_change_label(const OFX::Host::Param::Descriptor &descriptor)
{
	return QStringLiteral("Change %1")
		.arg(QString::fromStdString(descriptor.getName()));
}
void submit_undo_command(const std::shared_ptr<PluginNode> &node,
					   UndoCommand *command, const QString &label);

class NodeBoundParam {
public:
	virtual ~NodeBoundParam() = default;
	virtual void set_node(const std::shared_ptr<PluginNode> &node) = 0;
};

class PushbuttonInstance : public OFX::Host::Param::PushbuttonInstance,
						   public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor *descriptor_;

public:
	PushbuttonInstance(std::shared_ptr<PluginNode> effect,
					   const std::string &name,
					   OFX::Host::Param::Descriptor &descriptor,
					   OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::PushbuttonInstance(descriptor, param_set)
		, node_(effect)
	{
		descriptor_ = &descriptor;
	};
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
};

class IntegerInstance : public OFX::Host::Param::IntegerInstance,
						public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	QString id_;
	mutable std::mutex no_node_mutex_;
	bool has_value_ = false;
	int value_ = 0;

public:
	IntegerInstance(std::shared_ptr<PluginNode> node,
					OFX::Host::Param::Descriptor &descriptor,
					OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::IntegerInstance(descriptor, param_set)
		, node_(node)
		, descriptor_(descriptor)
		, id_(descriptor_.getName().c_str())
	{
		try {
			value_ = descriptor_.getProperties().getIntProperty(
				kOfxParamPropDefault);
			has_value_ = true;
		} catch (...) {
			value_ = 0;
			has_value_ = false;
		}
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &a) override
	{
		if (!node_) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			a = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		if (id_.isEmpty()) {
			return kOfxStatErrBadHandle;
		}
		QVariant variant = node_->get_standard_value(id_);

		if (variant.canConvert<int>()) {
			a = variant.toInt();
			return kOfxStatOK;
		}
		a = 0;
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, int &data) override
	{
		if (!node_) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			data = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		if (id_.isEmpty()) {
			return kOfxStatErrBadHandle;
		}
		QVariant variant =
			node_->get_value_at_time(id_, Rational::from_double(time));
		if (variant.canConvert<int>()) {
			data = variant.toInt();
			return kOfxStatOK;
		}
		data = 0;
		return kOfxStatErrValue;
	}
	OfxStatus set(int data) override
	{
		if (!node_) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_int, data);

		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, int data) override
	{
		if (!node_) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		Node::set_value_at_time(
			NodeInput(node_.get(), descriptor_.getName().c_str()),
			Rational::from_double(time), data, 0, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class DoubleInstance : public OFX::Host::Param::DoubleInstance,
					   public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_ = 0.0;

public:
	DoubleInstance(std::shared_ptr<PluginNode> effect, const std::string &name,
				   OFX::Host::Param::Descriptor &descriptor,
				   OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::DoubleInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
		try {
			value_ = descriptor_.getProperties().getDoubleProperty(
				kOfxParamPropDefault);
			has_value_ = true;
		} catch (...) {
			value_ = 0.0;
			has_value_ = false;
		}
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : 0.0;
			return kOfxStatOK;
		}
		QVariant variant =
			node_->get_standard_value(descriptor_.getName().c_str());
		if (variant.canConvert<double>()) {
			data = variant.toDouble();
			if (is_normalised_coordinate_system(descriptor_)) {
				double x_size, y_size;
				get_project_extent(x_size, y_size);
				data = to_normalised(data, x_size);
			}
			return kOfxStatOK;
		}
		data = 0.0;
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, double &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : 0.0;
			return kOfxStatOK;
		}
		QVariant variant = node_->get_value_at_time(descriptor_.getName().c_str(),
												Rational::from_double(time));
		if (variant.canConvert<double>()) {
			data = variant.toDouble();
			if (is_normalised_coordinate_system(descriptor_)) {
				double x_size, y_size;
				get_project_extent(x_size, y_size);
				data = to_normalised(data, x_size);
			}
			return kOfxStatOK;
		}
		data = 0.0;
		return kOfxStatErrValue;
	}
	OfxStatus set(double data) override
	{
		if (!node_) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		double val = data;
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			val = to_canonical(val, x_size);
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_float, val);
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, double data) override
	{
		if (!node_) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		double val = data;
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			val = to_canonical(val, x_size);
		}
		auto command = new MultiUndoCommand();
		Node::set_value_at_time(NodeInput(node_.get(),
									   descriptor_.getName().c_str()),
							 Rational::from_double(time), val, 0, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus derive(OfxTime, double &) override
	{
		return kOfxStatErrUnsupported;
	}
	OfxStatus integrate(OfxTime, OfxTime, double &) override
	{
		return kOfxStatErrUnsupported;
	}
};

class BooleanInstance : public OFX::Host::Param::BooleanInstance,
						public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	bool value_ = false;
	bool default_value() const
	{
		return descriptor_.getProperties().getIntProperty(
				   kOfxParamPropDefault) != 0;
	}

public:
	BooleanInstance(std::shared_ptr<PluginNode> effect, const std::string &name,
					OFX::Host::Param::Descriptor &descriptor,
					OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::BooleanInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
		value_ = default_value();
		has_value_ = true;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(bool &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : false;
			return kOfxStatOK;
		}
		QVariant variant =
			node_->get_standard_value(descriptor_.getName().c_str());
		if (variant.canConvert<bool>()) {
			data = variant.toBool();
			return kOfxStatOK;
		}
		data = default_value();
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, bool &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : false;
			return kOfxStatOK;
		}
		QVariant variant = node_->get_value_at_time(descriptor_.getName().c_str(),
												Rational::from_double(time));
		if (variant.isNull()) {
			qWarning().noquote()
				<< "Boolean get failed: Varient is null" << time
				<< Rational::from_double(time).to_double();
		}
		if (!variant.isValid()) {
			qWarning().noquote()
				<< "Boolean get failed: Varient is invalid" << time
				<< Rational::from_double(time).to_double();
		}
		if (variant.canConvert<bool>()) {
			data = variant.toBool();
			return kOfxStatOK;
		}
		data = default_value();
		return kOfxStatOK;
	}
	OfxStatus set(bool data) override
	{
		if (!node_) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_boolean, data);
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, bool data) override
	{
		if (!node_) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		Node::set_value_at_time(
			NodeInput(node_.get(), descriptor_.getName().c_str()),
			Rational::from_double(time), data, 0, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class ChoiceInstance : public OFX::Host::Param::ChoiceInstance,
					   public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	int value_ = 0;

public:
	ChoiceInstance(std::shared_ptr<PluginNode> effect, const std::string &name,
				   OFX::Host::Param::Descriptor &descriptor,
				   OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::ChoiceInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
		try {
			value_ = descriptor_.getProperties().getIntProperty(
				kOfxParamPropDefault);
			has_value_ = true;
		} catch (...) {
			value_ = 0;
			has_value_ = false;
		}
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		QVariant variant =
			node_->get_standard_value(descriptor_.getName().c_str());
		if (variant.canConvert<int>()) {
			data = variant.toInt();
			return kOfxStatOK;
		}
		data = 0;
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, int &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		QVariant variant = node_->get_value_at_time(descriptor_.getName().c_str(),
												Rational::from_double(time));
		if (variant.canConvert<int>()) {
			data = variant.toInt();
			return kOfxStatOK;
		}
		data = 0;
		return kOfxStatErrValue;
	}
	OfxStatus set(int data) override
	{
		if (!node_) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_combo, data);
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, int data) override
	{
		if (!node_) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		Node::set_value_at_time(
			NodeInput(node_.get(), descriptor_.getName().c_str()),
			Rational::from_double(time), data, 0, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class RGBAInstance : public OFX::Host::Param::RGBAInstance,
					 public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[4] = { 0.0, 0.0, 0.0, 0.0 };

public:
	RGBAInstance(std::shared_ptr<PluginNode> effect, const std::string &name,
				 OFX::Host::Param::Descriptor &descriptor,
				 OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::RGBAInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &r, double &g, double &b, double &a) override
	{
		if (!node_) {
			if (has_value_) {
				r = value_[0];
				g = value_[1];
				b = value_[2];
				a = value_[3];
			} else {
				r = g = b = a = 0.0;
			}
			return kOfxStatOK;
		}
		olive::core::Color c =
			node_->get_standard_value(descriptor_.getName().c_str())
				.value<olive::core::Color>();

		r = static_cast<double>(c.red());
		g = static_cast<double>(c.green());
		b = static_cast<double>(c.blue());
		a = static_cast<double>(c.alpha());
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, double &r, double &g, double &b, double &a) override
	{
		if (!node_) {
			if (has_value_) {
				r = value_[0];
				g = value_[1];
				b = value_[2];
				a = value_[3];
			} else {
				r = g = b = a = 0.0;
			}
			return kOfxStatOK;
		}
		olive::core::Color c =
			node_->get_value_at_time(descriptor_.getName().c_str(),
								 Rational::from_double(time))
				.value<olive::core::Color>();

		r = static_cast<double>(c.red());
		g = static_cast<double>(c.green());
		b = static_cast<double>(c.blue());
		a = static_cast<double>(c.alpha());
		return kOfxStatOK;
	}
	OfxStatus set(double r, double g, double b, double a) override
	{
		if (!node_) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			value_[3] = a;
			has_value_ = true;
			return kOfxStatOK;
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_color,
			QVariant::fromValue(olive::core::Color(r, g, b, a)));
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, double r, double g, double b, double a) override
	{
		if (!node_) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			value_[3] = a;
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		const QString name = descriptor_.getName().c_str();
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), r, 0, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), g, 1, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), b, 2, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), a, 3, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class RGBInstance : public OFX::Host::Param::RGBInstance,
					public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[3] = { 0.0, 0.0, 0.0 };

public:
	RGBInstance(std::shared_ptr<PluginNode> effect, const std::string &name,
				OFX::Host::Param::Descriptor &descriptor,
				OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::RGBInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &r, double &g, double &b) override
	{
		if (!node_) {
			if (has_value_) {
				r = value_[0];
				g = value_[1];
				b = value_[2];
			} else {
				r = g = b = 0.0;
			}
			return kOfxStatOK;
		}
		olive::core::Color c =
			node_->get_standard_value(descriptor_.getName().c_str())
				.value<olive::core::Color>();

		r = static_cast<double>(c.red());
		g = static_cast<double>(c.green());
		b = static_cast<double>(c.blue());
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, double &r, double &g, double &b) override
	{
		if (!node_) {
			if (has_value_) {
				r = value_[0];
				g = value_[1];
				b = value_[2];
			} else {
				r = g = b = 0.0;
			}
			return kOfxStatOK;
		}
		olive::core::Color c =
			node_->get_value_at_time(descriptor_.getName().c_str(),
								 Rational::from_double(time))
				.value<olive::core::Color>();

		r = static_cast<double>(c.red());
		g = static_cast<double>(c.green());
		b = static_cast<double>(c.blue());
		return kOfxStatOK;
	}
	OfxStatus set(double r, double g, double b) override
	{
		if (!node_) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			has_value_ = true;
			return kOfxStatOK;
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_color,
			QVariant::fromValue(olive::core::Color(r, g, b)));
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, double r, double g, double b) override
	{
		if (!node_) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		const QString name = descriptor_.getName().c_str();
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), r, 0, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), g, 1, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), b, 2, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class Double2DInstance : public OFX::Host::Param::Double2DInstance,
						 public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[2] = { 0.0, 0.0 };

public:
	Double2DInstance(std::shared_ptr<PluginNode> effect,
					 const std::string &name,
					 OFX::Host::Param::Descriptor &descriptor,
					 OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Double2DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &x, double &y) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0.0;
			}
			return kOfxStatOK;
		}
		QVector2D vec = node_->get_standard_value(descriptor_.getName().c_str())
							.value<QVector2D>();
		x = static_cast<double>(vec.x());
		y = static_cast<double>(vec.y());
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			x = to_normalised(x, x_size);
			y = to_normalised(y, y_size);
		}
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, double &x, double &y) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0.0;
			}
			return kOfxStatOK;
		}
		QVector2D vec = node_->get_value_at_time(descriptor_.getName().c_str(),
											 Rational::from_double(time))
							.value<QVector2D>();
		x = static_cast<double>(vec.x());
		y = static_cast<double>(vec.y());
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			x = to_normalised(x, x_size);
			y = to_normalised(y, y_size);
		}
		return kOfxStatOK;
	}
	OfxStatus set(double x, double y) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			has_value_ = true;
			return kOfxStatOK;
		}
		double xv = x, yv = y;
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			xv = to_canonical(xv, x_size);
			yv = to_canonical(yv, y_size);
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_vec2, QVector2D(xv, yv));
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, double x, double y) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			has_value_ = true;
			return kOfxStatOK;
		}
		double xv = x, yv = y;
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			xv = to_canonical(xv, x_size);
			yv = to_canonical(yv, y_size);
		}
		auto command = new MultiUndoCommand();
		const QString name = descriptor_.getName().c_str();
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), xv, 0, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), yv, 1, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class Integer2DInstance : public OFX::Host::Param::Integer2DInstance,
						  public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	int value_[2] = { 0, 0 };

public:
	Integer2DInstance(std::shared_ptr<PluginNode> effect,
					  const std::string &name,
					  OFX::Host::Param::Descriptor &descriptor,
					  OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Integer2DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &x, int &y) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0;
			}
			return kOfxStatOK;
		}
		QVector2D vec = node_->get_standard_value(descriptor_.getName().c_str())
							.value<QVector2D>();
		x = static_cast<int>(vec.x());
		y = static_cast<int>(vec.y());
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, int &x, int &y) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0;
			}
			return kOfxStatOK;
		}
		QVector2D vec = node_->get_value_at_time(descriptor_.getName().c_str(),
											 Rational::from_double(time))
							.value<QVector2D>();
		x = static_cast<int>(vec.x());
		y = static_cast<int>(vec.y());
		return kOfxStatOK;
	}
	OfxStatus set(int x, int y) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			has_value_ = true;
			return kOfxStatOK;
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_vec2, QVector2D(x, y));
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, int x, int y) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		const QString name = descriptor_.getName().c_str();
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), x, 0, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), y, 1, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class Double3DInstance : public OFX::Host::Param::Double3DInstance,
						 public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[3] = { 0.0, 0.0, 0.0 };

public:
	Double3DInstance(std::shared_ptr<PluginNode> effect,
					 const std::string &name,
					 OFX::Host::Param::Descriptor &descriptor,
					 OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Double3DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &x, double &y, double &z) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0.0;
			}
			return kOfxStatOK;
		}
		QVector3D vec = node_->get_standard_value(descriptor_.getName().c_str())
							.value<QVector3D>();
		x = static_cast<double>(vec.x());
		y = static_cast<double>(vec.y());
		z = static_cast<double>(vec.z());
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			x = to_normalised(x, x_size);
			y = to_normalised(y, y_size);
			z = to_normalised(z, x_size);
		}
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, double &x, double &y, double &z) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0.0;
			}
			return kOfxStatOK;
		}
		QVector3D vec = node_->get_value_at_time(descriptor_.getName().c_str(),
											 Rational::from_double(time))
							.value<QVector3D>();
		x = static_cast<double>(vec.x());
		y = static_cast<double>(vec.y());
		z = static_cast<double>(vec.z());
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			x = to_normalised(x, x_size);
			y = to_normalised(y, y_size);
			z = to_normalised(z, x_size);
		}
		return kOfxStatOK;
	}
	OfxStatus set(double x, double y, double z) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			value_[2] = z;
			has_value_ = true;
			return kOfxStatOK;
		}
		double xv = x, yv = y, zv = z;
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			xv = to_canonical(xv, x_size);
			yv = to_canonical(yv, y_size);
			zv = to_canonical(zv, x_size);
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_vec3, QVector3D(xv, yv, zv));
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, double x, double y, double z) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			value_[2] = z;
			has_value_ = true;
			return kOfxStatOK;
		}
		double xv = x, yv = y, zv = z;
		if (is_normalised_coordinate_system(descriptor_)) {
			double x_size, y_size;
			get_project_extent(x_size, y_size);
			xv = to_canonical(xv, x_size);
			yv = to_canonical(yv, y_size);
			zv = to_canonical(zv, x_size);
		}
		auto command = new MultiUndoCommand();
		const QString name = descriptor_.getName().c_str();
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), xv, 0, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), yv, 1, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), zv, 2, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class Integer3DInstance : public OFX::Host::Param::Integer3DInstance,
						  public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	int value_[3] = { 0, 0, 0 };

public:
	Integer3DInstance(std::shared_ptr<PluginNode> effect,
					  const std::string &name,
					  OFX::Host::Param::Descriptor &descriptor,
					  OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Integer3DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &x, int &y, int &z) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0;
			}
			return kOfxStatOK;
		}
		QVector3D vec = node_->get_standard_value(descriptor_.getName().c_str())
							.value<QVector3D>();
		x = static_cast<int>(vec.x());
		y = static_cast<int>(vec.y());
		z = static_cast<int>(vec.z());
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, int &x, int &y, int &z) override
	{
		if (!node_) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0;
			}
			return kOfxStatOK;
		}
		QVector3D vec = node_->get_value_at_time(descriptor_.getName().c_str(),
											 Rational::from_double(time))
							.value<QVector3D>();
		x = static_cast<int>(vec.x());
		y = static_cast<int>(vec.y());
		z = static_cast<int>(vec.z());
		return kOfxStatOK;
	}
	OfxStatus set(int x, int y, int z) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			value_[2] = z;
			has_value_ = true;
			return kOfxStatOK;
		}
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_vec3, QVector3D(x, y, z));
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, int x, int y, int z) override
	{
		if (!node_) {
			value_[0] = x;
			value_[1] = y;
			value_[2] = z;
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		const QString name = descriptor_.getName().c_str();
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), x, 0, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), y, 1, command, true);
		Node::set_value_at_time(NodeInput(node_.get(), name),
							 Rational::from_double(time), z, 2, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class StringInstance : public OFX::Host::Param::StringInstance,
					   public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	std::string value_;

public:
	StringInstance(std::shared_ptr<PluginNode> effect, const std::string &name,
				   OFX::Host::Param::Descriptor &descriptor,
				   OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::StringInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
		try {
			value_ = descriptor_.getProperties().getStringProperty(
				kOfxParamPropDefault);
			has_value_ = true;
		} catch (...) {
			value_.clear();
			has_value_ = false;
		}
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(std::string &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : std::string();
			return kOfxStatOK;
		}
		QVariant variant =
			node_->get_standard_value(descriptor_.getName().c_str());
		if (variant.canConvert<QString>()) {
			data = variant.toString().toStdString();
			return kOfxStatOK;
		}
		data.clear();
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, std::string &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : std::string();
			return kOfxStatOK;
		}
		QVariant variant = node_->get_value_at_time(descriptor_.getName().c_str(),
												Rational::from_double(time));
		if (variant.canConvert<QString>()) {
			data = variant.toString().toStdString();
			return kOfxStatOK;
		}
		data.clear();
		return kOfxStatErrValue;
	}
	OfxStatus set(const char *data) override
	{
		if (!node_) {
			value_ = data ? data : "";
			has_value_ = true;
			return kOfxStatOK;
		}
		QString v = QString::fromUtf8(data);
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_text, v);
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, const char *data) override
	{
		if (!node_) {
			value_ = data ? data : "";
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		Node::set_value_at_time(NodeInput(node_.get(),
									   descriptor_.getName().c_str()),
							 Rational::from_double(time),
							 QString::fromUtf8(data), 0, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class CustomInstance : public OFX::Host::Param::CustomInstance,
					   public NodeBoundParam {
protected:
	std::shared_ptr<PluginNode> node_;
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	std::string value_;

public:
	CustomInstance(std::shared_ptr<PluginNode> effect, const std::string &name,
				   OFX::Host::Param::Descriptor &descriptor,
				   OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::CustomInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(const std::shared_ptr<PluginNode> &new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(std::string &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : std::string();
			return kOfxStatOK;
		}
		QVariant variant =
			node_->get_standard_value(descriptor_.getName().c_str());
		if (variant.canConvert<QByteArray>()) {
			data = variant.toByteArray().toStdString();
			return kOfxStatOK;
		}
		if (variant.canConvert<QString>()) {
			data = variant.toString().toStdString();
			return kOfxStatOK;
		}
		data.clear();
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, std::string &data) override
	{
		if (!node_) {
			data = has_value_ ? value_ : std::string();
			return kOfxStatOK;
		}
		QVariant variant = node_->get_value_at_time(descriptor_.getName().c_str(),
												Rational::from_double(time));
		if (variant.canConvert<QByteArray>()) {
			data = variant.toByteArray().toStdString();
			return kOfxStatOK;
		}
		if (variant.canConvert<QString>()) {
			data = variant.toString().toStdString();
			return kOfxStatOK;
		}
		data.clear();
		return kOfxStatErrValue;
	}
	OfxStatus set(const char *data) override
	{
		if (!node_) {
			value_ = data ? data : "";
			has_value_ = true;
			return kOfxStatOK;
		}
		QByteArray v = QByteArray(data);
		SplitValue split = NodeValue::split_normal_value_into_track_values(
			NodeValue::k_binary, v);
		auto command = new NodeParamSetSplitStandardValueCommand(
			NodeInput(node_.get(), descriptor_.getName().c_str()), split);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, const char *data) override
	{
		if (!node_) {
			value_ = data ? data : "";
			has_value_ = true;
			return kOfxStatOK;
		}
		auto command = new MultiUndoCommand();
		Node::set_value_at_time(
			NodeInput(node_.get(), descriptor_.getName().c_str()),
			Rational::from_double(time), QByteArray(data), 0, command, true);
		submit_undo_command(node_, command, param_change_label(descriptor_));
		return kOfxStatOK;
	}
};

class GroupInstance : public OFX::Host::Param::GroupInstance {
public:
	GroupInstance(OFX::Host::Param::Descriptor &descriptor,
				  OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::GroupInstance(descriptor, param_set)
	{
	}
};

class PageInstance : public OFX::Host::Param::PageInstance {
public:
	PageInstance(OFX::Host::Param::Descriptor &descriptor,
				 OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::PageInstance(descriptor, param_set)
	{
	}
};
}
}

#endif // HOST_DEMO_PARAM_INSTANCE_H
