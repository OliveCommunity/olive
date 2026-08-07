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
 */

// Copyright OpenFX and contributors to the OpenFX project.
#ifndef OAK_PARAM_INSTANCE_H
#define OAK_PARAM_INSTANCE_H

#include <memory>
#include <mutex>
#include <string>

#include "ofxhParam.h"

#include "common/current.h"
#include "videoparams.h"
#include "node/node.h"
#include "olive/core/util/rational.h"
#include "undo/undocommand.h"

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
	// The Current video-params slot holds a heap
	// std::shared_ptr<olive::VideoParams> (facade sets it); fall back to
	// a 0-extent when unset.
	x_size = 0;
	y_size = 0;

	OakCurrent current = oakcommon_current_instance();
	void *slot = nullptr;
	oakcommon_current_get_video_params(current, &slot);
	oakcommon_current_free(&current);
	if (!slot) {
		return;
	}

	const auto &vp =
		*static_cast<std::shared_ptr<olive::VideoParams> *>(slot);
	if (!vp) {
		return;
	}

	double par = vp->pixel_aspect_ratio().to_double();
	x_size = vp->width() * par;
	y_size = vp->height();
}

inline double to_normalised(double canonical, double extent)
{
	return extent > 0 ? canonical / extent : canonical;
}

inline double to_canonical(double normalised, double extent)
{
	return extent > 0 ? normalised * extent : normalised;
}

inline std::string
param_change_label(const OFX::Host::Param::Descriptor &descriptor)
{
	return "Change " + descriptor.getName();
}

/**
 * @brief Route an undo command to the node's plugin instance (edit
 *        batching) or run it immediately when unbound.
 */
void submit_undo_command(OakNodeNode node, OakUndoCommand command,
						 const std::string &label);

class NodeBoundParam {
public:
	virtual ~NodeBoundParam() = default;
	virtual void set_node(OakNodeNode node) = 0;
};

namespace detail
{

inline oaknode_value value_int(int64_t v)
{
	oaknode_value value = {};
	value.type = OAKNODE_VALUE_INT;
	value.num = v;
	return value;
}

inline oaknode_value value_bool(bool v)
{
	oaknode_value value = {};
	value.type = OAKNODE_VALUE_BOOL;
	value.num = v ? 1 : 0;
	return value;
}

inline oaknode_value value_double(double v)
{
	oaknode_value value = {};
	value.type = OAKNODE_VALUE_FLOAT;
	value.f[0] = v;
	return value;
}

inline oaknode_value value_vec(double x, double y)
{
	oaknode_value value = {};
	value.type = OAKNODE_VALUE_VEC2;
	value.f[0] = x;
	value.f[1] = y;
	return value;
}

inline oaknode_value value_vec(double x, double y, double z)
{
	oaknode_value value = {};
	value.type = OAKNODE_VALUE_VEC3;
	value.f[0] = x;
	value.f[1] = y;
	value.f[2] = z;
	return value;
}

inline oaknode_value value_vec(double x, double y, double z, double w)
{
	oaknode_value value = {};
	value.type = OAKNODE_VALUE_VEC4;
	value.f[0] = x;
	value.f[1] = y;
	value.f[2] = z;
	value.f[3] = w;
	return value;
}

inline oaknode_value value_color(double r, double g, double b, double a)
{
	oaknode_value value = {};
	value.type = OAKNODE_VALUE_COLOR;
	value.f[0] = r;
	value.f[1] = g;
	value.f[2] = b;
	value.f[3] = a;
	return value;
}

/**
 * @brief Set an input instantly with an undo command and submit it
 */
inline OfxStatus node_set(OakNodeNode node, const std::string &id,
						  const oaknode_value &value,
						  const std::string &label)
{
	OakUndoCommand command = {};
	if (oaknode_node_set_input_undoable(node, id.c_str(), &value,
										&command) != OAKNODE_OK) {
		return kOfxStatErrValue;
	}
	submit_undo_command(node, command, label);
	return kOfxStatOK;
}

/**
 * @brief Set an input at a time (keyframe logic), one command per call
 */
inline OfxStatus node_set_at(OakNodeNode node, const std::string &id,
							 OfxTime time, const oaknode_value &value,
							 int track, const std::string &label)
{
	olive::core::Rational t = olive::core::Rational::from_double(time);
	OakUndoCommand command = {};
	if (oaknode_node_set_input_at_time_undoable(node, id.c_str(),
												t.numerator(),
												t.denominator(), &value,
												track,
												&command) != OAKNODE_OK) {
		return kOfxStatErrValue;
	}
	submit_undo_command(node, command, label);
	return kOfxStatOK;
}

/**
 * @brief Set multiple tracks at a time, batched into one undo command
 */
inline OfxStatus node_set_at_multi(OakNodeNode node, const std::string &id,
								   OfxTime time, const oaknode_value &value,
								   int track_count,
								   const std::string &label)
{
	olive::core::Rational t = olive::core::Rational::from_double(time);
	OakUndoCommand multi = oakundo_command_init_multi();
	if (!multi.ctx) {
		return kOfxStatErrMemory;
	}
	for (int track = 0; track < track_count; track++) {
		if (oaknode_node_set_input_at_time_into(node, id.c_str(),
												t.numerator(),
												t.denominator(), &value,
												track, multi) != OAKNODE_OK) {
			oakundo_command_free(&multi);
			return kOfxStatErrValue;
		}
	}
	submit_undo_command(node, multi, label);
	return kOfxStatOK;
}

inline bool node_get(OakNodeNode node, const std::string &id,
					 oaknode_value *out)
{
	return oaknode_node_get_input(node, id.c_str(), out) == OAKNODE_OK;
}

inline bool node_get_at(OakNodeNode node, const std::string &id,
						OfxTime time, oaknode_value *out)
{
	olive::core::Rational t = olive::core::Rational::from_double(time);
	return oaknode_node_get_input_at_time(node, id.c_str(), t.numerator(),
										  t.denominator(),
										  out) == OAKNODE_OK;
}

} // namespace detail

class PushbuttonInstance : public OFX::Host::Param::PushbuttonInstance,
						   public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor *descriptor_;

public:
	PushbuttonInstance(OakNodeNode effect, const std::string &name,
					   OFX::Host::Param::Descriptor &descriptor,
					   OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::PushbuttonInstance(descriptor, param_set)
		, node_(effect)
	{
		(void)name;
		descriptor_ = &descriptor;
	};
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
};

class IntegerInstance : public OFX::Host::Param::IntegerInstance,
						public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	std::string id_;
	mutable std::mutex no_node_mutex_;
	bool has_value_ = false;
	int value_ = 0;

public:
	IntegerInstance(OakNodeNode node,
					OFX::Host::Param::Descriptor &descriptor,
					OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::IntegerInstance(descriptor, param_set)
		, node_(node)
		, descriptor_(descriptor)
		, id_(descriptor_.getName())
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
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &a) override
	{
		if (!node_.ctx) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			a = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		if (id_.empty()) {
			return kOfxStatErrBadHandle;
		}
		oaknode_value value = {};
		if (detail::node_get(node_, id_, &value)) {
			a = int(value.num);
			return kOfxStatOK;
		}
		a = 0;
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, int &data) override
	{
		if (!node_.ctx) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			data = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		if (id_.empty()) {
			return kOfxStatErrBadHandle;
		}
		oaknode_value value = {};
		if (detail::node_get_at(node_, id_, time, &value)) {
			data = int(value.num);
			return kOfxStatOK;
		}
		data = 0;
		return kOfxStatErrValue;
	}
	OfxStatus set(int data) override
	{
		if (!node_.ctx) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set(node_, id_, detail::value_int(data),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, int data) override
	{
		if (!node_.ctx) {
			std::lock_guard<std::mutex> lock(no_node_mutex_);
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set_at(node_, id_, time,
								   detail::value_int(data), 0,
								   param_change_label(descriptor_));
	}
};

class DoubleInstance : public OFX::Host::Param::DoubleInstance,
					   public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_ = 0.0;

public:
	DoubleInstance(OakNodeNode effect, const std::string &name,
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
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : 0.0;
			return kOfxStatOK;
		}
		oaknode_value value = {};
		if (detail::node_get(node_, descriptor_.getName(), &value)) {
			data = value.f[0];
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
		if (!node_.ctx) {
			data = has_value_ ? value_ : 0.0;
			return kOfxStatOK;
		}
		oaknode_value value = {};
		if (detail::node_get_at(node_, descriptor_.getName(), time,
								&value)) {
			data = value.f[0];
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
		if (!node_.ctx) {
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
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_double(val),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, double data) override
	{
		if (!node_.ctx) {
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
		return detail::node_set_at(node_, descriptor_.getName(), time,
								   detail::value_double(val), 0,
								   param_change_label(descriptor_));
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
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	bool value_ = false;
	bool default_value() const
	{
		return descriptor_.getProperties().getIntProperty(
				   kOfxParamPropDefault) != 0;
	}

public:
	BooleanInstance(OakNodeNode effect, const std::string &name,
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
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(bool &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : false;
			return kOfxStatOK;
		}
		oaknode_value value = {};
		if (detail::node_get(node_, descriptor_.getName(), &value)) {
			data = value.num != 0;
			return kOfxStatOK;
		}
		data = default_value();
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, bool &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : false;
			return kOfxStatOK;
		}
		oaknode_value value = {};
		if (detail::node_get_at(node_, descriptor_.getName(), time,
								&value)) {
			data = value.num != 0;
			return kOfxStatOK;
		}
		data = default_value();
		return kOfxStatOK;
	}
	OfxStatus set(bool data) override
	{
		if (!node_.ctx) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_bool(data),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, bool data) override
	{
		if (!node_.ctx) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set_at(node_, descriptor_.getName(), time,
								   detail::value_bool(data), 0,
								   param_change_label(descriptor_));
	}
};

class ChoiceInstance : public OFX::Host::Param::ChoiceInstance,
					   public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	int value_ = 0;

public:
	ChoiceInstance(OakNodeNode effect, const std::string &name,
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
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		oaknode_value value = {};
		if (detail::node_get(node_, descriptor_.getName(), &value)) {
			data = int(value.num);
			return kOfxStatOK;
		}
		data = 0;
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, int &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : 0;
			return kOfxStatOK;
		}
		oaknode_value value = {};
		if (detail::node_get_at(node_, descriptor_.getName(), time,
								&value)) {
			data = int(value.num);
			return kOfxStatOK;
		}
		data = 0;
		return kOfxStatErrValue;
	}
	OfxStatus set(int data) override
	{
		if (!node_.ctx) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		oaknode_value value = detail::value_int(data);
		value.type = OAKNODE_VALUE_COMBO;
		return detail::node_set(node_, descriptor_.getName(), value,
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, int data) override
	{
		if (!node_.ctx) {
			value_ = data;
			has_value_ = true;
			return kOfxStatOK;
		}
		oaknode_value value = detail::value_int(data);
		value.type = OAKNODE_VALUE_COMBO;
		return detail::node_set_at(node_, descriptor_.getName(), time,
								   value, 0, param_change_label(descriptor_));
	}
};

class RGBAInstance : public OFX::Host::Param::RGBAInstance,
					 public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[4] = { 0.0, 0.0, 0.0, 0.0 };

public:
	RGBAInstance(OakNodeNode effect, const std::string &name,
				 OFX::Host::Param::Descriptor &descriptor,
				 OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::RGBAInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &r, double &g, double &b, double &a) override
	{
		if (!node_.ctx) {
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
		oaknode_value value = {};
		detail::node_get(node_, descriptor_.getName(), &value);
		r = value.f[0];
		g = value.f[1];
		b = value.f[2];
		a = value.f[3];
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, double &r, double &g, double &b,
				  double &a) override
	{
		if (!node_.ctx) {
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
		oaknode_value value = {};
		detail::node_get_at(node_, descriptor_.getName(), time, &value);
		r = value.f[0];
		g = value.f[1];
		b = value.f[2];
		a = value.f[3];
		return kOfxStatOK;
	}
	OfxStatus set(double r, double g, double b, double a) override
	{
		if (!node_.ctx) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			value_[3] = a;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_color(r, g, b, a),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, double r, double g, double b,
				  double a) override
	{
		if (!node_.ctx) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			value_[3] = a;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set_at_multi(node_, descriptor_.getName(), time,
										 detail::value_color(r, g, b, a), 4,
										 param_change_label(descriptor_));
	}
};

class RGBInstance : public OFX::Host::Param::RGBInstance,
					public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[3] = { 0.0, 0.0, 0.0 };

public:
	RGBInstance(OakNodeNode effect, const std::string &name,
				OFX::Host::Param::Descriptor &descriptor,
				OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::RGBInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &r, double &g, double &b) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				r = value_[0];
				g = value_[1];
				b = value_[2];
			} else {
				r = g = b = 0.0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get(node_, descriptor_.getName(), &value);
		r = value.f[0];
		g = value.f[1];
		b = value.f[2];
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, double &r, double &g, double &b) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				r = value_[0];
				g = value_[1];
				b = value_[2];
			} else {
				r = g = b = 0.0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get_at(node_, descriptor_.getName(), time, &value);
		r = value.f[0];
		g = value.f[1];
		b = value.f[2];
		return kOfxStatOK;
	}
	OfxStatus set(double r, double g, double b) override
	{
		if (!node_.ctx) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_color(r, g, b, 1.0),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, double r, double g, double b) override
	{
		if (!node_.ctx) {
			value_[0] = r;
			value_[1] = g;
			value_[2] = b;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set_at_multi(node_, descriptor_.getName(), time,
										 detail::value_color(r, g, b, 1.0),
										 3, param_change_label(descriptor_));
	}
};

class Double2DInstance : public OFX::Host::Param::Double2DInstance,
						 public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[2] = { 0.0, 0.0 };

public:
	Double2DInstance(OakNodeNode effect, const std::string &name,
					 OFX::Host::Param::Descriptor &descriptor,
					 OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Double2DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &x, double &y) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0.0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get(node_, descriptor_.getName(), &value);
		x = value.f[0];
		y = value.f[1];
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
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0.0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get_at(node_, descriptor_.getName(), time, &value);
		x = value.f[0];
		y = value.f[1];
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
		if (!node_.ctx) {
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
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_vec(xv, yv),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, double x, double y) override
	{
		if (!node_.ctx) {
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
		return detail::node_set_at_multi(node_, descriptor_.getName(), time,
										 detail::value_vec(xv, yv), 2,
										 param_change_label(descriptor_));
	}
};

class Integer2DInstance : public OFX::Host::Param::Integer2DInstance,
						  public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	int value_[2] = { 0, 0 };

public:
	Integer2DInstance(OakNodeNode effect, const std::string &name,
					  OFX::Host::Param::Descriptor &descriptor,
					  OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Integer2DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &x, int &y) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get(node_, descriptor_.getName(), &value);
		x = int(value.f[0]);
		y = int(value.f[1]);
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, int &x, int &y) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
			} else {
				x = y = 0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get_at(node_, descriptor_.getName(), time, &value);
		x = int(value.f[0]);
		y = int(value.f[1]);
		return kOfxStatOK;
	}
	OfxStatus set(int x, int y) override
	{
		if (!node_.ctx) {
			value_[0] = x;
			value_[1] = y;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_vec(double(x), double(y)),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, int x, int y) override
	{
		if (!node_.ctx) {
			value_[0] = x;
			value_[1] = y;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set_at_multi(node_, descriptor_.getName(), time,
										 detail::value_vec(double(x),
														   double(y)),
										 2, param_change_label(descriptor_));
	}
};

class Double3DInstance : public OFX::Host::Param::Double3DInstance,
						 public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	double value_[3] = { 0.0, 0.0, 0.0 };

public:
	Double3DInstance(OakNodeNode effect, const std::string &name,
					 OFX::Host::Param::Descriptor &descriptor,
					 OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Double3DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(double &x, double &y, double &z) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0.0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get(node_, descriptor_.getName(), &value);
		x = value.f[0];
		y = value.f[1];
		z = value.f[2];
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
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0.0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get_at(node_, descriptor_.getName(), time, &value);
		x = value.f[0];
		y = value.f[1];
		z = value.f[2];
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
		if (!node_.ctx) {
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
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_vec(xv, yv, zv),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, double x, double y, double z) override
	{
		if (!node_.ctx) {
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
		return detail::node_set_at_multi(node_, descriptor_.getName(), time,
										 detail::value_vec(xv, yv, zv), 3,
										 param_change_label(descriptor_));
	}
};

class Integer3DInstance : public OFX::Host::Param::Integer3DInstance,
						  public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	int value_[3] = { 0, 0, 0 };

public:
	Integer3DInstance(OakNodeNode effect, const std::string &name,
					  OFX::Host::Param::Descriptor &descriptor,
					  OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::Integer3DInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(int &x, int &y, int &z) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get(node_, descriptor_.getName(), &value);
		x = int(value.f[0]);
		y = int(value.f[1]);
		z = int(value.f[2]);
		return kOfxStatOK;
	}
	OfxStatus get(OfxTime time, int &x, int &y, int &z) override
	{
		if (!node_.ctx) {
			if (has_value_) {
				x = value_[0];
				y = value_[1];
				z = value_[2];
			} else {
				x = y = z = 0;
			}
			return kOfxStatOK;
		}
		oaknode_value value = {};
		detail::node_get_at(node_, descriptor_.getName(), time, &value);
		x = int(value.f[0]);
		y = int(value.f[1]);
		z = int(value.f[2]);
		return kOfxStatOK;
	}
	OfxStatus set(int x, int y, int z) override
	{
		if (!node_.ctx) {
			value_[0] = x;
			value_[1] = y;
			value_[2] = z;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set(node_, descriptor_.getName(),
								detail::value_vec(double(x), double(y),
												  double(z)),
								param_change_label(descriptor_));
	}
	OfxStatus set(OfxTime time, int x, int y, int z) override
	{
		if (!node_.ctx) {
			value_[0] = x;
			value_[1] = y;
			value_[2] = z;
			has_value_ = true;
			return kOfxStatOK;
		}
		return detail::node_set_at_multi(node_, descriptor_.getName(), time,
										 detail::value_vec(double(x),
														   double(y),
														   double(z)),
										 3, param_change_label(descriptor_));
	}
};

class StringInstance : public OFX::Host::Param::StringInstance,
					   public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	std::string value_;

public:
	StringInstance(OakNodeNode effect, const std::string &name,
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
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(std::string &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : std::string();
			return kOfxStatOK;
		}
		int needed = oaknode_node_get_input_string(
			node_, descriptor_.getName().c_str(), nullptr, 0);
		if (needed > 0) {
			data.resize(size_t(needed) - 1);
			oaknode_node_get_input_string(node_, descriptor_.getName().c_str(),
										  data.data(), needed);
			return kOfxStatOK;
		}
		data.clear();
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, std::string &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : std::string();
			return kOfxStatOK;
		}
		// String inputs are not keyframable; the standard value is the
		// value at any time
		(void)time;
		return get(data);
	}
	OfxStatus set(const char *data) override
	{
		if (!node_.ctx) {
			value_ = data ? data : "";
			has_value_ = true;
			return kOfxStatOK;
		}
		OakUndoCommand command = {};
		if (oaknode_node_set_input_string_undoable(
				node_, descriptor_.getName().c_str(), data ? data : "",
				&command) != OAKNODE_OK) {
			return kOfxStatErrValue;
		}
		submit_undo_command(node_, command,
							param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, const char *data) override
	{
		(void)time;
		return set(data);
	}
};

class CustomInstance : public OFX::Host::Param::CustomInstance,
					   public NodeBoundParam {
protected:
	OakNodeNode node_ = {};
	OFX::Host::Param::Descriptor &descriptor_;
	bool has_value_ = false;
	std::string value_;

public:
	CustomInstance(OakNodeNode effect, const std::string &name,
				   OFX::Host::Param::Descriptor &descriptor,
				   OFX::Host::Param::SetInstance *param_set = nullptr)
		: OFX::Host::Param::CustomInstance(descriptor, param_set)
		, node_(effect)
		, descriptor_(descriptor)
	{
		(void)name;
	}
	void set_node(OakNodeNode new_node) override
	{
		node_ = new_node;
	}
	OfxStatus get(std::string &data) override
	{
		if (!node_.ctx) {
			data = has_value_ ? value_ : std::string();
			return kOfxStatOK;
		}
		int needed = oaknode_node_get_input_string(
			node_, descriptor_.getName().c_str(), nullptr, 0);
		if (needed > 0) {
			data.resize(size_t(needed) - 1);
			oaknode_node_get_input_string(node_, descriptor_.getName().c_str(),
										  data.data(), needed);
			return kOfxStatOK;
		}
		data.clear();
		return kOfxStatErrValue;
	}
	OfxStatus get(OfxTime time, std::string &data) override
	{
		(void)time;
		return get(data);
	}
	OfxStatus set(const char *data) override
	{
		if (!node_.ctx) {
			value_ = data ? data : "";
			has_value_ = true;
			return kOfxStatOK;
		}
		OakUndoCommand command = {};
		if (oaknode_node_set_input_string_undoable(
				node_, descriptor_.getName().c_str(), data ? data : "",
				&command) != OAKNODE_OK) {
			return kOfxStatErrValue;
		}
		submit_undo_command(node_, command,
							param_change_label(descriptor_));
		return kOfxStatOK;
	}
	OfxStatus set(OfxTime time, const char *data) override
	{
		(void)time;
		return set(data);
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

#endif // OAK_PARAM_INSTANCE_H
