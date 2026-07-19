/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "serializer210528.h"

#include "config/config.h"
#include "node/factory.h"
#include "node/group/group.h"

namespace olive
{

ProjectSerializer210528::LoadData
ProjectSerializer210528::load(Project *project, QXmlStreamReader *reader,
							  LoadType load_type, void *reserved) const
{
	XMLNodeData xml_node_data;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("uuid")) {
			project->set_uuid(QUuid::fromString(reader->readElementText()));

		} else if (reader->name() == QStringLiteral("nodes")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("node")) {
					bool is_root = false;
					bool is_cm = false;
					bool is_settings = false;
					QString id;

					{
						XMLAttributeLoop(reader, attr)
						{
							if (attr.name() == QStringLiteral("id")) {
								id = attr.value().toString();
							} else if (attr.name() == QStringLiteral("root") &&
									   attr.value() == QStringLiteral("1")) {
								is_root = true;
							} else if (attr.name() == QStringLiteral("cm") &&
									   attr.value() == QStringLiteral("1")) {
								is_cm = true;
							} else if (attr.name() ==
										   QStringLiteral("settings") &&
									   attr.value() == QStringLiteral("1")) {
								is_settings = true;
							}
						}
					}

					if (id.isEmpty()) {
						qWarning() << "Failed to load node with empty ID";
						reader->skipCurrentElement();
					} else {
						Node *node;
						bool handled_elsewhere = false;

						if (is_root) {
							project->initialize();
							node = project->root();
						} else if (is_cm) {
							load_color_manager(reader, project);
							handled_elsewhere = true;
						} else if (is_settings) {
							load_project_settings(reader, project);
							handled_elsewhere = true;
						} else {
							node = NodeFactory::create_from_id(id);
						}

						if (!handled_elsewhere) {
							if (!node) {
								qWarning()
									<< "Failed to find node with ID" << id;
								reader->skipCurrentElement();
							} else {
								load_node(node, xml_node_data, reader);
								node->setParent(project);
							}
						}
					}
				} else {
					reader->skipCurrentElement();
				}
			}

		} else if (reader->name() == QStringLiteral("positions")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("context")) {
					quintptr context_ptr = 0;
					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("ptr")) {
							context_ptr = attr.value().toULongLong();
							break;
						}
					}

					Node *context = xml_node_data.node_ptrs.value(context_ptr);

					if (!context) {
						qWarning() << "Failed to find pointer for context";
						reader->skipCurrentElement();
					} else {
						while (xml_read_next_start_element(reader)) {
							if (reader->name() == QStringLiteral("node")) {
								quintptr node_ptr;
								Node::Position node_pos;

								if (load_position(reader, &node_ptr,
												 &node_pos)) {
									Node *node =
										xml_node_data.node_ptrs.value(node_ptr);

									if (node) {
										context->set_node_position_in_context(
											node, node_pos);
									} else {
										qWarning()
											<< "Failed to find pointer for node position";
										reader->skipCurrentElement();
									}
								}
							} else {
								reader->skipCurrentElement();
							}
						}
					}

				} else {
					reader->skipCurrentElement();
				}
			}

		} else {
			// Skip this
			reader->skipCurrentElement();
		}
	}

	// Make connections
	post_connect(xml_node_data);

	// Resolve tracks
	for (Node *n : project->nodes()) {
		n->set_caches_enabled(true);

		if (Track *t = dynamic_cast<Track *>(n)) {
			for (int i = 0; i < t->input_array_size(Track::k_block_input); i++) {
				Block *b = static_cast<Block *>(
					t->get_connected_output(Track::k_block_input, i));
				if (!b->track()) {
					t->append_block(b);
				}
			}
		}
	}

	return LoadData();
}

void ProjectSerializer210528::load_node(Node *node, XMLNodeData &xml_node_data,
									   QXmlStreamReader *reader) const
{
	while (xml_read_next_start_element(reader)) {
		if (is_cancelled()) {
			return;
		}

		if (reader->name() == QStringLiteral("input")) {
			load_input(node, reader, xml_node_data);
		} else if (reader->name() == QStringLiteral("ptr")) {
			xml_node_data.node_ptrs.insert(
				reader->readElementText().toULongLong(), node);
		} else if (reader->name() == QStringLiteral("label")) {
			node->set_label(reader->readElementText());
		} else if (reader->name() == QStringLiteral("color")) {
			node->set_override_color(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("links")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("link")) {
					xml_node_data.block_links.append(
						{ node, reader->readElementText().toULongLong() });
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("custom")) {
			load_node_custom(reader, node, xml_node_data);

		} else if (reader->name() == QStringLiteral("connections")) {
			// Load connections
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("connection")) {
					QString param_id;
					int ele = -1;

					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("element")) {
							ele = attr.value().toInt();
						} else if (attr.name() == QStringLiteral("input")) {
							param_id = attr.value().toString();
						}
					}

					QString output_node_id;
					QString output_param_id;

					while (xml_read_next_start_element(reader)) {
						if (reader->name() == QStringLiteral("node")) {
							output_node_id = reader->readElementText();
						} else if (reader->name() == QStringLiteral("output")) {
							output_param_id = reader->readElementText();
						} else {
							reader->skipCurrentElement();
						}
					}

					xml_node_data.desired_connections.append(
						{ NodeInput(node, param_id, ele),
						  output_node_id.toULongLong(), output_param_id });
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("hints")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("hint")) {
					QString input;
					int element = -1;

					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("input")) {
							input = attr.value().toString();
						} else if (attr.name() == QStringLiteral("element")) {
							element = attr.value().toInt();
						}
					}

					Node::ValueHint vh;
					load_value_hint(&vh, reader);
					node->set_value_hint_for_input(input, vh, element);
				} else {
					reader->skipCurrentElement();
				}
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	node->LoadFinishedEvent();
}

void ProjectSerializer210528::load_color_manager(QXmlStreamReader *reader,
											   Project *project) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("input")) {
			QString id;
			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("id")) {
					id = attr.value().toString();
				}
			}

			if (id == QStringLiteral("config") ||
				id == QStringLiteral("default_input") ||
				id == QStringLiteral("reference_space")) {
				QString value;

				while (xml_read_next_start_element(reader)) {
					if (reader->name() == QStringLiteral("primary")) {
						while (xml_read_next_start_element(reader)) {
							if (reader->name() == QStringLiteral("standard")) {
								while (xml_read_next_start_element(reader)) {
									if (reader->name() ==
										QStringLiteral("track")) {
										value = reader->readElementText();
									} else {
										reader->skipCurrentElement();
									}
								}
							} else {
								reader->skipCurrentElement();
							}
						}
					} else {
						reader->skipCurrentElement();
					}
				}

				if (id == QStringLiteral("default_input")) {
					// Default color space
					// NOTE: Stupidly, we saved these as integers which means we can't add anything to the OCIO
					//       config. So we must convert back to string here.
					static const QStringList list = {
						QStringLiteral("Linear"),
						QStringLiteral("CIE-XYZ D65"),
						QStringLiteral("Filmic Log Encoding"),
						QStringLiteral("sRGB OETF"),
						QStringLiteral("Apple DCI-P3 D65"),
						QStringLiteral("AppleP3 sRGB OETF"),
						QStringLiteral("BT.1886 EOTF"),
						QStringLiteral("AppleP3 Filmic Log Encoding"),
						QStringLiteral("BT.1886 Filmic Log Encoding"),
						QStringLiteral("Fuji F-Log OETF"),
						QStringLiteral("Fuji F-Log F-Gamut"),
						QStringLiteral("Panasonic V-Log V-Gamut"),
						QStringLiteral("Arri Wide Gamut / LogC EI 800"),
						QStringLiteral("Arri Wide Gamut / LogC EI 400"),
						QStringLiteral("Blackmagic Film Wide Gamut (Gen 5)"),
						QStringLiteral("Rec.709 OETF"),
						QStringLiteral("Non-Colour Data")
					};
					int num_value = value.toInt();
					value = list.at(num_value);
					project->set_default_input_color_space(value);
				} else if (id == QStringLiteral("reference_space")) {
					// Reference space
					if (value == QStringLiteral("1")) {
						value = ocio::ROLE_COMPOSITING_LOG;
					} else {
						value = ocio::ROLE_SCENE_LINEAR;
					}
					project->set_color_reference_space(value);
				} else {
					// Config filename
					project->set_color_config_filename(value);
				}
			} else {
				reader->skipCurrentElement();
			}
		} else {
			reader->skipCurrentElement();
		}
	}
}

void ProjectSerializer210528::load_project_settings(QXmlStreamReader *reader,
												  Project *project) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("input")) {
			QString id;
			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("id")) {
					id = attr.value().toString();
				}
			}

			if (id == QStringLiteral("cache_setting") ||
				id == QStringLiteral("cache_path")) {
				QString value;

				while (xml_read_next_start_element(reader)) {
					if (reader->name() == QStringLiteral("primary")) {
						while (xml_read_next_start_element(reader)) {
							if (reader->name() == QStringLiteral("standard")) {
								while (xml_read_next_start_element(reader)) {
									if (reader->name() ==
										QStringLiteral("track")) {
										value = reader->readElementText();
									} else {
										reader->skipCurrentElement();
									}
								}
							} else {
								reader->skipCurrentElement();
							}
						}
					} else {
						reader->skipCurrentElement();
					}
				}

				if (id == QStringLiteral("cache_setting")) {
					project->set_cache_location_setting(
						static_cast<Project::CacheSetting>(value.toInt()));
				} else {
					project->set_custom_cache_path(value);
				}
			} else {
				reader->skipCurrentElement();
			}
		} else {
			reader->skipCurrentElement();
		}
	}
}

void ProjectSerializer210528::load_input(Node *node, QXmlStreamReader *reader,
										XMLNodeData &xml_node_data) const
{
	QString param_id;

	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("id")) {
			param_id = attr.value().toString();

			break;
		}
	}

	if (param_id.isEmpty()) {
		qWarning() << "Failed to load parameter with missing ID";
		reader->skipCurrentElement();
		return;
	}

	if (!node->has_input_with_id(param_id)) {
		qWarning() << "Failed to load parameter that didn't exist:" << param_id;
		reader->skipCurrentElement();
		return;
	}

	while (xml_read_next_start_element(reader)) {
		if (is_cancelled()) {
			return;
		}

		if (reader->name() == QStringLiteral("primary")) {
			// Load primary immediate
			load_immediate(reader, node, param_id, -1, xml_node_data);
		} else if (reader->name() == QStringLiteral("subelements")) {
			// Load subelements
			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("count")) {
					node->input_array_resize(param_id, attr.value().toInt());
				}
			}

			int element_counter = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("element")) {
					load_immediate(reader, node, param_id, element_counter,
								  xml_node_data);

					element_counter++;
				} else {
					reader->skipCurrentElement();
				}
			}
		} else {
			reader->skipCurrentElement();
		}
	}
}

void ProjectSerializer210528::load_immediate(QXmlStreamReader *reader,
											Node *node, const QString &input,
											int element,
											XMLNodeData &xml_node_data) const
{
	Q_UNUSED(xml_node_data)

	NodeValue::Type data_type = node->get_input_data_type(input);

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("standard")) {
			// Load standard value
			int val_index = 0;

			while (xml_read_next_start_element(reader)) {
				if (is_cancelled()) {
					return;
				}

				if (reader->name() == QStringLiteral("track")) {
					QVariant value_on_track;

					if (data_type == NodeValue::k_video_params) {
						VideoParams vp;
						vp.load(reader);
						value_on_track = QVariant::fromValue(vp);
					} else if (data_type == NodeValue::k_audio_params) {
						AudioParams ap =
							TypeSerializer::load_audio_params(reader);
						value_on_track = QVariant::fromValue(ap);
					} else {
						QString value_text = reader->readElementText();

						if (!value_text.isEmpty()) {
							value_on_track = NodeValue::string_to_value(
								data_type, value_text, true);
						}
					}

					node->set_split_standard_value_on_track(input, val_index,
													   value_on_track, element);

					val_index++;
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("keyframing") &&
				   node->is_input_keyframable(input)) {
			node->set_input_is_keyframing(input, reader->readElementText().toInt(),
									   element);
		} else if (reader->name() == QStringLiteral("keyframes")) {
			int track = 0;

			while (xml_read_next_start_element(reader)) {
				if (is_cancelled()) {
					return;
				}

				if (reader->name() == QStringLiteral("track")) {
					while (xml_read_next_start_element(reader)) {
						if (is_cancelled()) {
							return;
						}

						if (reader->name() == QStringLiteral("key")) {
							QString key_input;
							Rational key_time;
							NodeKeyframe::Type key_type = NodeKeyframe::k_linear;
							QVariant key_value;
							QPointF key_in_handle;
							QPointF key_out_handle;

							XMLAttributeLoop(reader, attr)
							{
								if (is_cancelled()) {
									return;
								}

								if (attr.name() == QStringLiteral("input")) {
									key_input = attr.value().toString();
								} else if (attr.name() ==
										   QStringLiteral("time")) {
									key_time = Rational::from_string(
										attr.value().toString().toStdString());
								} else if (attr.name() ==
										   QStringLiteral("type")) {
									key_type = static_cast<NodeKeyframe::Type>(
										attr.value().toInt());
								} else if (attr.name() ==
										   QStringLiteral("inhandlex")) {
									key_in_handle.setX(attr.value().toDouble());
								} else if (attr.name() ==
										   QStringLiteral("inhandley")) {
									key_in_handle.setY(attr.value().toDouble());
								} else if (attr.name() ==
										   QStringLiteral("outhandlex")) {
									key_out_handle.setX(
										attr.value().toDouble());
								} else if (attr.name() ==
										   QStringLiteral("outhandley")) {
									key_out_handle.setY(
										attr.value().toDouble());
								}
							}

							key_value = NodeValue::string_to_value(
								data_type, reader->readElementText(), true);

							NodeKeyframe *key = new NodeKeyframe(
								key_time, key_value, key_type, track, element,
								key_input, node);
							key->set_bezier_control_in(key_in_handle);
							key->set_bezier_control_out(key_out_handle);
						} else {
							reader->skipCurrentElement();
						}
					}

					track++;
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("csinput")) {
			node->set_input_property(input, QStringLiteral("col_input"),
								   reader->readElementText());
		} else if (reader->name() == QStringLiteral("csdisplay")) {
			node->set_input_property(input, QStringLiteral("col_display"),
								   reader->readElementText());
		} else if (reader->name() == QStringLiteral("csview")) {
			node->set_input_property(input, QStringLiteral("col_view"),
								   reader->readElementText());
		} else if (reader->name() == QStringLiteral("cslook")) {
			node->set_input_property(input, QStringLiteral("col_look"),
								   reader->readElementText());
		} else {
			reader->skipCurrentElement();
		}
	}
}

bool ProjectSerializer210528::load_position(QXmlStreamReader *reader,
										   quintptr *node_ptr,
										   Node::Position *pos) const
{
	bool got_node_ptr = false;
	bool got_pos_x = false;
	bool got_pos_y = false;

	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("ptr")) {
			*node_ptr = attr.value().toULongLong();
			got_node_ptr = true;
			break;
		}
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("x")) {
			pos->position.setX(reader->readElementText().toDouble());
			got_pos_x = true;
		} else if (reader->name() == QStringLiteral("y")) {
			pos->position.setY(reader->readElementText().toDouble());
			got_pos_y = true;
		} else if (reader->name() == QStringLiteral("expanded")) {
			pos->expanded = reader->readElementText().toInt();
		} else {
			reader->skipCurrentElement();
		}
	}

	return got_node_ptr && got_pos_x && got_pos_y;
}

void ProjectSerializer210528::post_connect(const XMLNodeData &xml_node_data) const
{
	foreach (const XMLNodeData::SerializedConnection &con,
			 xml_node_data.desired_connections) {
		if (Node *out = xml_node_data.node_ptrs.value(con.output_node)) {
			// Use output param as hint tag since we grandfathered those in
			Node::ValueHint hint(con.output_param);

			Node::connect_edge(out, con.input);

			con.input.node()->set_value_hint_for_input(con.input.input(), hint,
												   con.input.element());
		}
	}

	foreach (const XMLNodeData::BlockLink &l, xml_node_data.block_links) {
		Node *a = l.block;
		Node *b = xml_node_data.node_ptrs.value(l.link);

		Node::link(a, b);
	}

	foreach (const XMLNodeData::GroupLink &l, xml_node_data.group_input_links) {
		if (Node *input_node = xml_node_data.node_ptrs.value(l.input_node)) {
			NodeInput resolved(input_node, l.input_id, l.input_element);

			l.group->add_input_passthrough(resolved);
		}
	}

	for (auto it = xml_node_data.group_output_links.cbegin();
		 it != xml_node_data.group_output_links.cend(); it++) {
		if (Node *output_node = xml_node_data.node_ptrs.value(it.value())) {
			it.key()->set_output_passthrough(output_node);
		}
	}
}

void ProjectSerializer210528::load_node_custom(QXmlStreamReader *reader,
											 Node *node,
											 XMLNodeData &xml_node_data) const
{
	// Viewer-based nodes
	if (ViewerOutput *viewer = dynamic_cast<ViewerOutput *>(node)) {
		Footage *footage = dynamic_cast<Footage *>(node);

		while (xml_read_next_start_element(reader)) {
			if (reader->name() == QStringLiteral("points")) {
				load_timeline_points(reader, viewer);
			} else if (reader->name() == QStringLiteral("timestamp") &&
					   footage) {
				footage->set_timestamp(reader->readElementText().toLongLong());
			} else {
				reader->skipCurrentElement();
			}
		}

	} else if (Track *track = dynamic_cast<Track *>(node)) {
		while (xml_read_next_start_element(reader)) {
			if (reader->name() == QStringLiteral("height")) {
				track->set_track_height(reader->readElementText().toDouble());
			} else {
				reader->skipCurrentElement();
			}
		}

	} else if (NodeGroup *group = dynamic_cast<NodeGroup *>(node)) {
		while (xml_read_next_start_element(reader)) {
			if (reader->name() == QStringLiteral("inputpassthroughs")) {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == QStringLiteral("inputpassthrough")) {
						XMLNodeData::GroupLink link;

						link.group = group;

						while (xml_read_next_start_element(reader)) {
							if (reader->name() == QStringLiteral("node")) {
								link.input_node =
									reader->readElementText().toULongLong();
							} else if (reader->name() ==
									   QStringLiteral("input")) {
								link.input_id = reader->readElementText();
							} else if (reader->name() ==
									   QStringLiteral("element")) {
								link.input_element =
									reader->readElementText().toInt();
							} else {
								reader->skipCurrentElement();
							}
						}

						xml_node_data.group_input_links.append(link);
					} else {
						reader->skipCurrentElement();
					}
				}
			} else if (reader->name() == QStringLiteral("outputpassthrough")) {
				xml_node_data.group_output_links.insert(
					group, reader->readElementText().toULongLong());
			} else {
				reader->skipCurrentElement();
			}
		}

	} else {
		reader->skipCurrentElement();
	}
}

void ProjectSerializer210528::load_timeline_points(QXmlStreamReader *reader,
												 ViewerOutput *points) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("markers")) {
			load_marker_list(reader, points->get_markers());
		} else if (reader->name() == QStringLiteral("workarea")) {
			load_work_area(reader, points->get_work_area());
		} else {
			reader->skipCurrentElement();
		}
	}
}

void ProjectSerializer210528::load_work_area(QXmlStreamReader *reader,
										   TimelineWorkArea *workarea) const
{
	Rational range_in = workarea->in();
	Rational range_out = workarea->out();

	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("enabled")) {
			workarea->set_enabled(attr.value() != QStringLiteral("0"));
		} else if (attr.name() == QStringLiteral("in")) {
			range_in =
				Rational::from_string(attr.value().toString().toStdString());
		} else if (attr.name() == QStringLiteral("out")) {
			range_out =
				Rational::from_string(attr.value().toString().toStdString());
		}
	}

	TimeRange loaded_workarea(range_in, range_out);

	if (loaded_workarea != workarea->range()) {
		workarea->set_range(loaded_workarea);
	}

	reader->skipCurrentElement();
}

void ProjectSerializer210528::load_marker_list(QXmlStreamReader *reader,
											 TimelineMarkerList *markers) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("marker")) {
			QString name;
			Rational in, out;

			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() == QStringLiteral("name")) {
					name = attr.value().toString();
				} else if (attr.name() == QStringLiteral("in")) {
					in = Rational::from_string(
						attr.value().toString().toStdString());
				} else if (attr.name() == QStringLiteral("out")) {
					out = Rational::from_string(
						attr.value().toString().toStdString());
				}
			}

			new TimelineMarker(OAK_CONFIG("MarkerColor").toInt(),
							   TimeRange(in, out), name, markers);
		}

		reader->skipCurrentElement();
	}
}

void ProjectSerializer210528::load_value_hint(Node::ValueHint *hint,
											QXmlStreamReader *reader) const
{
	QVector<NodeValue::Type> types;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("types")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("type")) {
					types.append(static_cast<NodeValue::Type>(
						reader->readElementText().toInt()));
				} else {
					reader->skipCurrentElement();
				}
			}
		} else if (reader->name() == QStringLiteral("index")) {
			hint->set_index(reader->readElementText().toInt());
		} else if (reader->name() == QStringLiteral("tag")) {
			hint->set_tag(reader->readElementText());
		} else {
			reader->skipCurrentElement();
		}
	}

	hint->set_type(types);
}

}
