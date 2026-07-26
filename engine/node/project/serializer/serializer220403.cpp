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

#include "serializer220403.h"

#include "config/config.h"
#include "node/factory.h"
#include "node/group/group.h"

namespace olive
{

ProjectSerializer220403::LoadData
ProjectSerializer220403::load(Project *project, QXmlStreamReader *reader,
							  LoadType load_type, void *reserved) const
{
	QMap<quintptr, QMap<QString, QString>> properties;
	QMap<quintptr, QMap<quintptr, Node::Position>> positions;
	XMLNodeData xml_node_data;

	LoadData load_data;

	if ((load_type == k_project &&
		 reader->name() == QStringLiteral("project")) ||
		((load_type == k_only_nodes &&
		  reader->name() == QStringLiteral("nodes")) ||
		 (load_type == k_only_clips &&
		  reader->name() == QStringLiteral("timeline"))) ||
		(load_type == k_only_keyframes &&
		 reader->name() == QStringLiteral("keyframes")) ||
		(load_type == k_only_markers &&
		 reader->name() == QStringLiteral("markers"))) {
		while (xml_read_next_start_element(reader)) {
			if (reader->name() == QStringLiteral("layout")) {
				// Since the main window's functions have to occur in the GUI thread (and we're likely
				// loading in a secondary thread), we load all necessary data into a separate struct so we
				// can continue loading and queue it with the main window so it can handle the data
				// appropriately in its own thread.

				load_data.layout = SerializedLayoutInfo::from_xml(
					reader, xml_node_data.node_ptrs);

			} else if (reader->name() == QStringLiteral("uuid")) {
				if (project) {
					project->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else {
					reader->skipCurrentElement();
				}

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
								} else if (attr.name() ==
											   QStringLiteral("root") &&
										   attr.value() ==
											   QStringLiteral("1")) {
									is_root = true;
								} else if (attr.name() ==
											   QStringLiteral("cm") &&
										   attr.value() ==
											   QStringLiteral("1")) {
									is_cm = true;
								} else if (attr.name() ==
											   QStringLiteral("settings") &&
										   attr.value() ==
											   QStringLiteral("1")) {
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
									if (project) {
										node->setParent(project);
									} else {
										load_data.nodes.append(node);
									}
								}
							}
						}
					} else {
						reader->skipCurrentElement();
					}
				}

			} else if (reader->name() == QStringLiteral("keyframes")) {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == QStringLiteral("node")) {
						QString node_id;
						XMLAttributeLoop(reader, attr)
						{
							if (attr.name() == QStringLiteral("id")) {
								node_id = attr.value().toString();
								break;
							}
						}

						Node *n = nullptr;
						if (!node_id.isEmpty()) {
							n = NodeFactory::create_from_id(node_id);
						}

						if (!n) {
							reader->skipCurrentElement();
						} else {
							while (xml_read_next_start_element(reader)) {
								if (reader->name() == QStringLiteral("input")) {
									QString input_id;
									XMLAttributeLoop(reader, attr)
									{
										if (attr.name() ==
											QStringLiteral("id")) {
											input_id = attr.value().toString();
											break;
										}
									}

									if (input_id.isEmpty()) {
										reader->skipCurrentElement();
									} else {
										while (
											xml_read_next_start_element(reader)) {
											if (reader->name() ==
												QStringLiteral("element")) {
												QString element_id;
												XMLAttributeLoop(reader, attr)
												{
													if (attr.name() ==
														QStringLiteral("id")) {
														element_id =
															attr.value()
																.toString();
														break;
													}
												}

												if (element_id.isEmpty()) {
													reader->skipCurrentElement();
												} else {
													while (
														xml_read_next_start_element(
															reader)) {
														if (reader->name() ==
															QStringLiteral(
																"track")) {
															QString track_id;
															XMLAttributeLoop(
																reader, attr)
															{
																if (attr.name() ==
																	QStringLiteral(
																		"id")) {
																	track_id =
																		attr.value()
																			.toString();
																	break;
																}
															}

															if (track_id
																	.isEmpty()) {
																reader
																	->skipCurrentElement();
															} else {
																while (
																	xml_read_next_start_element(
																		reader)) {
																	if (reader
																			->name() ==
																		QStringLiteral(
																			"key")) {
																		NodeKeyframe
																			*key =
																				new NodeKeyframe();
																		key->set_input(
																			input_id);
																		key->set_element(
																			element_id
																				.toInt());
																		key->set_track(
																			track_id
																				.toInt());

																		load_keyframe(
																			reader,
																			key,
																			n->get_input_data_type(
																				input_id));

																		load_data
																			.keyframes
																				[node_id]
																			.append(
																				key);
																	} else {
																		reader
																			->skipCurrentElement();
																	}
																}
															}
														} else {
															reader
																->skipCurrentElement();
														}
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
						}

						delete n;
					} else {
						reader->skipCurrentElement();
					}
				}

			} else if (reader->name() == QStringLiteral("markers")) {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == QStringLiteral("marker")) {
						TimelineMarker *marker = new TimelineMarker();
						load_marker(reader, marker);
						load_data.markers.push_back(marker);
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

						if (context_ptr) {
							while (xml_read_next_start_element(reader)) {
								if (reader->name() == QStringLiteral("node")) {
									quintptr node_ptr;
									Node::Position node_pos;

									if (load_position(reader, &node_ptr,
													 &node_pos)) {
										if (node_ptr) {
											positions[context_ptr].insert(
												node_ptr, node_pos);
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
						} else {
							qWarning()
								<< "Attempted to load context with no pointer";
							reader->skipCurrentElement();
						}

					} else {
						reader->skipCurrentElement();
					}
				}

			} else if (reader->name() == QStringLiteral("properties")) {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == QStringLiteral("node")) {
						quintptr ptr = 0;

						XMLAttributeLoop(reader, attr)
						{
							if (attr.name() == QStringLiteral("ptr")) {
								ptr = attr.value().toULongLong();

								// Only attribute we're looking for right now
								break;
							}
						}

						if (ptr) {
							QMap<QString, QString> properties_for_node;
							while (xml_read_next_start_element(reader)) {
								properties_for_node.insert(
									reader->name().toString(),
									reader->readElementText());
							}
							properties.insert(ptr, properties_for_node);
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
	}

	// Resolve positions
	for (auto it = positions.cbegin(); it != positions.cend(); it++) {
		Node *ctx = xml_node_data.node_ptrs.value(it.key());
		if (ctx) {
			for (auto jt = it.value().cbegin(); jt != it.value().cend(); jt++) {
				Node *n = xml_node_data.node_ptrs.value(jt.key());
				if (n) {
					ctx->set_node_position_in_context(n, jt.value());
				}
			}
		}
	}

	// Make connections
	post_connect(xml_node_data);

	load_data.node_ptrs = xml_node_data.node_ptrs;
	load_data.node_uuids = xml_node_data.node_uuids;

	// Resolve serialized properties (if any)
	for (auto it = properties.cbegin(); it != properties.cend(); it++) {
		Node *node = xml_node_data.node_ptrs.value(it.key());
		if (node) {
			load_data.properties.insert(node, it.value());
		}
	}

	// Re-enable caches and resolve tracks
	const QVector<Node *> &nodes = project ? project->nodes() : load_data.nodes;
	for (Node *n : nodes) {
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

		// Clear duplicate label (to facilitate #2147)
		if (ClipBlock *c = dynamic_cast<ClipBlock *>(n)) {
			if (c->connected_viewer() &&
				c->get_label() == c->connected_viewer()->get_label()) {
				c->set_label(QString());
			}
		}
	}

	return load_data;
}

void ProjectSerializer220403::load_node(Node *node, XMLNodeData &xml_node_data,
									   QXmlStreamReader *reader) const
{
	while (xml_read_next_start_element(reader)) {
		if (is_cancelled()) {
			return;
		}

		if (reader->name() == QStringLiteral("input")) {
			load_input(node, reader, xml_node_data);
		} else if (reader->name() == QStringLiteral("ptr")) {
			quintptr ptr = reader->readElementText().toULongLong();
			xml_node_data.node_ptrs.insert(ptr, node);
		} else if (reader->name() == QStringLiteral("label")) {
			node->set_label(reader->readElementText());
		} else if (reader->name() == QStringLiteral("uuid")) {
			xml_node_data.node_uuids.insert(
				node, QUuid::fromString(reader->readElementText()));
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

					while (xml_read_next_start_element(reader)) {
						if (reader->name() == QStringLiteral("output")) {
							output_node_id = reader->readElementText();
						} else {
							reader->skipCurrentElement();
						}
					}

					xml_node_data.desired_connections.append(
						{ NodeInput(node, param_id, ele),
						  output_node_id.toULongLong() });
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
		} else if (reader->name() == QStringLiteral("caches")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("audio")) {
					node->audio_playback_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else if (reader->name() == QStringLiteral("video")) {
					node->video_frame_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else if (reader->name() == QStringLiteral("thumb")) {
					node->thumbnail_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
				} else if (reader->name() == QStringLiteral("waveform")) {
					node->waveform_cache()->set_uuid(
						QUuid::fromString(reader->readElementText()));
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

void ProjectSerializer220403::load_color_manager(QXmlStreamReader *reader,
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

void ProjectSerializer220403::load_project_settings(QXmlStreamReader *reader,
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

void ProjectSerializer220403::load_input(Node *node, QXmlStreamReader *reader,
										XMLNodeData &xml_node_data) const
{
	if (dynamic_cast<NodeGroup *>(node)) {
		// Ignore input of group
		reader->skipCurrentElement();
		return;
	}

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

void ProjectSerializer220403::load_immediate(QXmlStreamReader *reader,
											Node *node, const QString &input,
											int element,
											XMLNodeData &xml_node_data) const
{
	Q_UNUSED(xml_node_data)

	NodeValue::Type data_type = node->get_input_data_type(input);

	// HACK: SubtitleParams contain the actual subtitle data, so loading/replacing it will overwrite
	//       the valid subtitles. We hack around it by simply skipping loading subtitles, we'll see
	//       if this ends up being an issue in the future.
	if (data_type == NodeValue::k_subtitle_params) {
		reader->skipCurrentElement();
		return;
	}

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
							NodeKeyframe *key = new NodeKeyframe();
							key->set_input(input);
							key->set_element(element);
							key->set_track(track);

							load_keyframe(reader, key, data_type);
							key->setParent(node);
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

void ProjectSerializer220403::load_keyframe(QXmlStreamReader *reader,
										   NodeKeyframe *key,
										   NodeValue::Type data_type) const
{
	QString key_input;
	QPointF key_in_handle;
	QPointF key_out_handle;

	XMLAttributeLoop(reader, attr)
	{
		if (is_cancelled()) {
			return;
		}

		if (attr.name() == QStringLiteral("input")) {
			key_input = attr.value().toString();
		} else if (attr.name() == QStringLiteral("time")) {
			key->set_time(
				Rational::from_string(attr.value().toString().toStdString()));
		} else if (attr.name() == QStringLiteral("type")) {
			key->set_type_no_bezier_adj(
				static_cast<NodeKeyframe::Type>(attr.value().toInt()));
		} else if (attr.name() == QStringLiteral("inhandlex")) {
			key_in_handle.setX(attr.value().toDouble());
		} else if (attr.name() == QStringLiteral("inhandley")) {
			key_in_handle.setY(attr.value().toDouble());
		} else if (attr.name() == QStringLiteral("outhandlex")) {
			key_out_handle.setX(attr.value().toDouble());
		} else if (attr.name() == QStringLiteral("outhandley")) {
			key_out_handle.setY(attr.value().toDouble());
		}
	}

	key->set_value(
		NodeValue::string_to_value(data_type, reader->readElementText(), true));

	key->set_bezier_control_in(key_in_handle);
	key->set_bezier_control_out(key_out_handle);
}

bool ProjectSerializer220403::load_position(QXmlStreamReader *reader,
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

void ProjectSerializer220403::post_connect(const XMLNodeData &xml_node_data) const
{
	foreach (const XMLNodeData::SerializedConnection &con,
			 xml_node_data.desired_connections) {
		if (Node *out = xml_node_data.node_ptrs.value(con.output_node)) {
			Node::connect_edge(out, con.input);
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

			l.group->add_input_passthrough(resolved, l.passthrough_id);

			l.group->set_input_flag(l.passthrough_id,
								  InputFlag(l.custom_flags.value()));

			if (!l.custom_name.isEmpty()) {
				l.group->set_input_name(l.passthrough_id, l.custom_name);
			}

			l.group->set_input_data_type(l.passthrough_id, l.data_type);

			l.group->set_default_value(l.passthrough_id, l.default_val);

			for (auto it = l.custom_properties.cbegin();
				 it != l.custom_properties.cend(); it++) {
				l.group->set_input_property(l.passthrough_id, it.key(),
										  it.value());
			}
		}
	}

	for (auto it = xml_node_data.group_output_links.cbegin();
		 it != xml_node_data.group_output_links.cend(); it++) {
		if (Node *output_node = xml_node_data.node_ptrs.value(it.value())) {
			it.key()->set_output_passthrough(output_node);
		}
	}
}

void ProjectSerializer220403::load_node_custom(QXmlStreamReader *reader,
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
							} else if (reader->name() == QStringLiteral("id")) {
								link.passthrough_id = reader->readElementText();
							} else if (reader->name() ==
									   QStringLiteral("name")) {
								link.custom_name = reader->readElementText();
							} else if (reader->name() ==
									   QStringLiteral("flags")) {
								link.custom_flags = InputFlags(
									reader->readElementText().toULongLong());
							} else if (reader->name() ==
									   QStringLiteral("type")) {
								link.data_type = NodeValue::get_data_type_from_name(
									reader->readElementText());
							} else if (reader->name() ==
									   QStringLiteral("default")) {
								link.default_val = NodeValue::string_to_value(
									link.data_type, reader->readElementText(),
									false);
							} else if (reader->name() ==
									   QStringLiteral("properties")) {
								while (xml_read_next_start_element(reader)) {
									if (reader->name() ==
										QStringLiteral("property")) {
										QString key;
										QString value;

										while (
											xml_read_next_start_element(reader)) {
											if (reader->name() ==
												QStringLiteral("key")) {
												key = reader->readElementText();
											} else if (reader->name() ==
													   QStringLiteral(
														   "value")) {
												value =
													reader->readElementText();
											} else {
												reader->skipCurrentElement();
											}
										}

										if (!key.isEmpty()) {
											link.custom_properties.insert(
												key, value);
										}
									} else {
										reader->skipCurrentElement();
									}
								}
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

void ProjectSerializer220403::load_timeline_points(QXmlStreamReader *reader,
												 ViewerOutput *viewer) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("markers")) {
			load_marker_list(reader, viewer->get_markers());
		} else if (reader->name() == QStringLiteral("workarea")) {
			load_work_area(reader, viewer->get_work_area());
		} else {
			reader->skipCurrentElement();
		}
	}
}

void ProjectSerializer220403::load_marker(QXmlStreamReader *reader,
										 TimelineMarker *marker) const
{
	Rational in, out;

	XMLAttributeLoop(reader, attr)
	{
		if (attr.name() == QStringLiteral("name")) {
			marker->set_name(attr.value().toString());
		} else if (attr.name() == QStringLiteral("in")) {
			in = Rational::from_string(attr.value().toString().toStdString());
		} else if (attr.name() == QStringLiteral("out")) {
			out = Rational::from_string(attr.value().toString().toStdString());
		} else if (attr.name() == QStringLiteral("color")) {
			marker->set_color(attr.value().toInt());
		}
	}

	marker->set_time(TimeRange(in, out));

	// This element has no inner text, so just skip it
	reader->skipCurrentElement();
}

void ProjectSerializer220403::load_work_area(QXmlStreamReader *reader,
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

void ProjectSerializer220403::load_marker_list(QXmlStreamReader *reader,
											 TimelineMarkerList *markers) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("marker")) {
			TimelineMarker *marker = new TimelineMarker(markers);
			load_marker(reader, marker);
		} else {
			reader->skipCurrentElement();
		}
	}
}

void ProjectSerializer220403::load_value_hint(Node::ValueHint *hint,
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
