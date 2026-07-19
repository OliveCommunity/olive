/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
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
#include <QString>

#include <QString>

#include "serializer230220.h"

#include "config/config.h"
#include "node/factory.h"
#include "node/group/group.h"
#include "node/serializeddata.h"
#include <QtCore>
namespace olive
{

ProjectSerializer230220::LoadData
ProjectSerializer230220::load(Project *project, QXmlStreamReader *reader,
							  LoadType load_type, void *reserved) const
{
	QMap<quintptr, QMap<QString, QString>> properties;
	QMap<quintptr, QMap<quintptr, Node::Position>> positions;
	LoadData load_data;
	SerializedData project_data;

	switch (load_type) {
	case k_project: {
		if (reader->name() == QStringLiteral("project")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("project")) {
					project_data = project->load(reader);
					load_data.node_ptrs = project_data.node_ptrs;
				} else if (reader->name() == QStringLiteral("layout")) {
					load_data.layout = MainWindowLayoutInfo::from_xml(
						reader, project_data.node_ptrs);
				} else {
					reader->skipCurrentElement();
				}
			}

			post_connect(project->nodes(), &project_data);
		} else {
			reader->skipCurrentElement();
		}
		break;
	}
	case k_only_markers: {
		if (reader->name() == QStringLiteral("markers")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("marker")) {
					TimelineMarker *marker = new TimelineMarker();
					marker->load(reader);
					load_data.markers.push_back(marker);
				} else {
					reader->skipCurrentElement();
				}
			}
		} else {
			reader->skipCurrentElement();
		}
		break;
	}
	case k_only_keyframes: {
		if (reader->name() == QStringLiteral("keyframes")) {
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
									if (attr.name() == QStringLiteral("id")) {
										input_id = attr.value().toString();
										break;
									}
								}

								if (input_id.isEmpty()) {
									reader->skipCurrentElement();
								} else {
									while (xml_read_next_start_element(reader)) {
										if (reader->name() ==
											QStringLiteral("element")) {
											QString element_id;
											XMLAttributeLoop(reader, attr)
											{
												if (attr.name() ==
													QStringLiteral("id")) {
													element_id =
														attr.value().toString();
													break;
												}
											}

											if (element_id.isEmpty()) {
												reader->skipCurrentElement();
											} else {
												while (xml_read_next_start_element(
													reader)) {
													if (reader->name() ==
														QStringLiteral(
															"track")) {
														QString track_id;
														XMLAttributeLoop(reader,
																		 attr)
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

														if (track_id.isEmpty()) {
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
																	NodeKeyframe *key =
																		new NodeKeyframe();
																	key->set_input(
																		input_id);
																	key->set_element(
																		element_id
																			.toInt());
																	key->set_track(
																		track_id
																			.toInt());

																	key->load(
																		reader,
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
		} else {
			reader->skipCurrentElement();
		}
		break;
	}
	case k_only_clips:
	case k_only_nodes: {
		if ((load_type == k_only_nodes &&
			 reader->name() == QStringLiteral("nodes")) ||
			(load_type == k_only_clips &&
			 reader->name() == QStringLiteral("timeline"))) {
			QMap<quintptr, Node *> skipped_items;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("node")) {
					QString id;
					quintptr ptr = 0;
					QVector<quintptr> items;

					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("id")) {
							id = attr.value().toString();
						} else if (attr.name() == QStringLiteral("ptr")) {
							ptr = attr.value().toULongLong();
						} else if (attr.name() == QStringLiteral("items")) {
							QVector<QStringView> l = attr.value().split(',');
							items.reserve(l.size());
							for (const QStringView &s : l) {
								items.append(s.toULongLong());
							}
						}
					}

					if (id.isEmpty()) {
						qWarning() << "Failed to load node with empty ID";
						reader->skipCurrentElement();
					} else {
						bool dependency_of_item = false;

						if (project && !items.empty()) {
							for (quintptr p : items) {
								if (project->nodes().contains(
										reinterpret_cast<Node *>(p))) {
									dependency_of_item = true;
									break;
								}
							}
						}

						if (dependency_of_item) {
							reader->skipCurrentElement();
						} else {
							Node *node = NodeFactory::create_from_id(id);
							if (!node) {
								qWarning()
									<< "Failed to find node with ID" << id;
								reader->skipCurrentElement();
							} else {
								if (project && node->is_item() && ptr) {
									// If we're pasting an object into the same project, we should re-use the item
									// rather than duplicate.
									Node *existing =
										reinterpret_cast<Node *>(ptr);
									if (project->nodes().contains(existing)) {
										// Connect this
										skipped_items.insert(ptr, existing);

										// Don't continue loading this
										delete node;
										node = nullptr;

										// Skip element
										reader->skipCurrentElement();
									}
								}

								if (node) {
									// Disable cache while node is being loaded (we'll re-enable it later)
									node->set_caches_enabled(false);
									node->load(reader, &project_data);
									load_data.nodes.append(node);
								}
							}
						}
					}

					load_data.node_ptrs = project_data.node_ptrs;
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
					reader->skipCurrentElement();
				}
			}

			if (!skipped_items.empty()) {
				for (auto it = project_data.desired_connections.begin();
					 it != project_data.desired_connections.end();) {
					const SerializedData::SerializedConnection &sc = *it;

					if (Node *si = skipped_items.value(sc.output_node)) {
						// Convert this to a promised connection
						Node::OutputConnection oc = { si, sc.input };
						load_data.promised_connections.push_back(oc);
						it = project_data.desired_connections.erase(it);
					} else {
						it++;
					}
				}
			}

			// Resolve serialized properties (if any)
			for (auto it = properties.cbegin(); it != properties.cend(); it++) {
				Node *node = project_data.node_ptrs.value(it.key());
				if (node) {
					load_data.properties.insert(node, it.value());
				}
			}

			post_connect(load_data.nodes, &project_data);
		} else {
			reader->skipCurrentElement();
		}
		break;
	}
	}

	return load_data;
}

void write_node_map(QXmlStreamWriter *writer, Node *node,
				  const QVector<Node *> &nodes)
{
	writer->writeStartElement(QStringLiteral("node"));

	writer->writeAttribute(QStringLiteral("ptr"),
						   QString::number(reinterpret_cast<quintptr>(node)));

	for (auto oc : node->output_connections()) {
		if (nodes.contains(oc.second.node())) {
			write_node_map(writer, oc.second.node(), nodes);
		}
	}

	writer->writeEndElement();
}

void ProjectSerializer230220::save(QXmlStreamWriter *writer,
								   const SaveData &data, void *reserved) const
{
	if (!data.get_only_serialize_markers().empty()) {
		writer->writeStartElement(QStringLiteral("markers"));

		writer->writeAttribute(QStringLiteral("version"), QString::number(1));

		for (auto it = data.get_only_serialize_markers().cbegin();
			 it != data.get_only_serialize_markers().cend(); it++) {
			TimelineMarker *marker = *it;
			writer->writeStartElement(QStringLiteral("marker"));
			marker->save(writer);
			writer->writeEndElement(); // marker
		}

		writer->writeEndElement(); // markers
	} else if (!data.get_only_serialize_keyframes().empty()) {
		writer->writeStartElement(QStringLiteral("keyframes"));

		writer->writeAttribute(QStringLiteral("version"), QString::number(1));

		// Organize keyframes into node+input
		QHash<QString,
			  QHash<QString, QMap<int, QMap<int, QVector<NodeKeyframe *>>>>>
			organized;

		for (auto it = data.get_only_serialize_keyframes().cbegin();
			 it != data.get_only_serialize_keyframes().cend(); it++) {
			NodeKeyframe *key = *it;
			organized[key->parent()->id()][key->input()][key->element()]
					 [key->track()]
						 .append(key);
		}

		for (auto it = organized.cbegin(); it != organized.cend(); it++) {
			writer->writeStartElement(QStringLiteral("node"));

			writer->writeAttribute(QStringLiteral("id"), it.key());

			for (auto jt = it.value().cbegin(); jt != it.value().cend(); jt++) {
				writer->writeStartElement(QStringLiteral("input"));

				writer->writeAttribute(QStringLiteral("id"), jt.key());

				for (auto kt = jt.value().cbegin(); kt != jt.value().cend();
					 kt++) {
					writer->writeStartElement(QStringLiteral("element"));

					writer->writeAttribute(QStringLiteral("id"),
										   QString::number(kt.key()));

					for (auto lt = kt.value().cbegin(); lt != kt.value().cend();
						 lt++) {
						const QVector<NodeKeyframe *> &keys = lt.value();

						writer->writeStartElement(QStringLiteral("track"));

						writer->writeAttribute(QStringLiteral("id"),
											   QString::number(lt.key()));

						for (NodeKeyframe *key : keys) {
							writer->writeStartElement(QStringLiteral("key"));
							key->save(writer, key->parent()->get_input_data_type(
												  key->input()));
							writer->writeEndElement(); // key
						}

						writer->writeEndElement(); // track
					}

					writer->writeEndElement(); // element
				}

				writer->writeEndElement(); // input
			}

			writer->writeEndElement(); // node;
		}

		writer->writeEndElement(); // keyframes
	} else if (!data.get_only_serialize_nodes().empty()) {
		if (data.type() == k_only_clips) {
			writer->writeStartElement(QStringLiteral("timeline"));
		} else {
			writer->writeStartElement(QStringLiteral("nodes"));
		}

		writer->writeAttribute(QStringLiteral("version"), QString::number(1));

		for (Node *n : data.get_only_serialize_nodes()) {
			writer->writeStartElement(QStringLiteral("node"));

			QStringList item_list;
			for (Node *i : data.get_only_serialize_nodes()) {
				if (i->is_item() && i->inputs_from(n, true)) {
					item_list.append(
						QString::number(reinterpret_cast<quintptr>(i)));
				}
			}
			if (!item_list.empty()) {
				writer->writeAttribute(QStringLiteral("items"),
									   item_list.join(','));
			}

			n->save(writer);
			writer->writeEndElement(); // node
		}

		if (!data.get_properties().empty()) {
			writer->writeStartElement(QStringLiteral("properties"));
			for (auto it = data.get_properties().cbegin();
				 it != data.get_properties().cend(); it++) {
				writer->writeStartElement(QStringLiteral("node"));

				writer->writeAttribute(
					QStringLiteral("ptr"),
					QString::number(reinterpret_cast<quintptr>(it.key())));

				for (auto jt = it.value().cbegin(); jt != it.value().cend();
					 jt++) {
					writer->writeTextElement(jt.key(), jt.value());
				}

				writer->writeEndElement(); // node
			}
			writer->writeEndElement(); // properties
		}

		writer->writeEndElement(); // nodes
	} else if (Project *project = data.get_project()) {
		writer->writeStartElement(QStringLiteral("project"));

		writer->writeStartElement(QStringLiteral("project"));
		project->save(writer);
		writer->writeEndElement(); // project

		writer->writeStartElement(QStringLiteral("layout"));
		data.get_layout().to_xml(writer);
		writer->writeEndElement(); // layout

		writer->writeEndElement(); // project
	} else {
		qCritical() << "ProjectSerializer provided nothing to save";
	}
}

void ProjectSerializer230220::post_connect(const QVector<Node *> &nodes,
										  SerializedData *project_data) const
{
	foreach (const SerializedData::SerializedConnection &con,
			 project_data->desired_connections) {
		if (Node *out = project_data->node_ptrs.value(con.output_node)) {
			Node::connect_edge(out, con.input);
		}
	}

	foreach (const SerializedData::BlockLink &l, project_data->block_links) {
		Node *a = l.block;
		Node *b = project_data->node_ptrs.value(l.link);

		Node::link(a, b);
	}

	for (auto it = nodes.cbegin(); it != nodes.cend(); it++) {
		Node *n = *it;

		n->PostLoadEvent(project_data);

		n->set_caches_enabled(true);
	}
}

}
