#include "ODrive.h"

typedef std::vector<uint8_t> serial_buffer;
using nlohmann::json;


static void populate_from_json(json& j, Endpoint& endpoints) {
	for (json& obj : j) {
		std::string name = obj["name"];
		std::string type = obj["type"];
		std::string access = "none";

		if (obj.count("access")) {
			access = obj["access"];
		}
		int id = obj["id"];		
		Endpoint& endpoint = endpoints.add_child(name, type, id, access);
		if (obj.count("members")) {
			json& members = obj["members"];
			populate_from_json(members, endpoint);
		}
	}
}


int ODrive::endpoint_request(int endpoint_id, serial_buffer& received_payload, serial_buffer payload, int ack, int length) {
	serial_buffer send_buffer;
	serial_buffer receive_buffer;
	unsigned char receive_bytes[100] = { 0 };
	int sent_bytes = 0;
	int received_bytes = 0;
	int max_bytes_to_receive = 64;
	int timeout = 1000;
	int i = 0;
	static short seq_no = 0;
	short received_seq_no = 0;
	int CRC = 7230;

	if (ack) {
		endpoint_id |= 0x8000;
	}
	seq_no = (seq_no + 1) & 0x7fff;
	seq_no |= 0x80;

	// Send the packet
	send_to_odrive(handle, create_odrive_packet(seq_no, endpoint_id, (short)length, (serial_buffer)payload), &sent_bytes);

	// Immediatly wait for response from Odrive and check if ack (if we asked for one)
	receive_from_odrive(handle, receive_bytes, max_bytes_to_receive, &received_bytes, timeout);

	for (i = 0; i < received_bytes; i++) {
		receive_buffer.push_back(receive_bytes[i]);
	}

	serial_buffer::iterator it = receive_buffer.begin();
	received_payload = decode_odrive_packet(it, received_seq_no, receive_buffer);

	// return the response payload
	return received_payload.size();
}


Endpoint ODrive::get_json_interface() {
	serial_buffer send_payload;
	serial_buffer receive_payload;
	serial_buffer received_json;
	Endpoint root(this,0);

	int received_bytes = 0;
	int total_received = 0;

	do {
		serialize(send_payload, (int)total_received);
		received_bytes = endpoint_request(0, receive_payload, send_payload, 1, 64);
		send_payload.clear();
		total_received += received_bytes;
		for (uint8_t byte : receive_payload) {
			received_json.push_back(byte);
		}
	} while (received_bytes > 0);
	j = json::parse(received_json);
	//printf("Received %i bytes!\n", total_received);
	populate_from_json(j, root);
	return root;
}


void ODrive::get_float(int id, float& value) {
	serial_buffer send_payload;
	serial_buffer receive_payload;
	endpoint_request(id, receive_payload, send_payload, 1, 4);
	deserialize(receive_payload.begin(), value);
}


void ODrive::get_int(int id, int& value) {
	serial_buffer send_payload;
	serial_buffer receive_payload;
	endpoint_request(id, receive_payload, send_payload, 1, 4);
	deserialize(receive_payload.begin(), value);
}


void ODrive::set_float(int id, float& value) {
	serial_buffer send_payload;
	serial_buffer receive_payload;
	serialize(send_payload, value);
	endpoint_request(id, receive_payload, send_payload, 1, 4);
}


void ODrive::set_int(int id, int& value) {
	serial_buffer send_payload;
	serial_buffer receive_payload;
	serialize(send_payload, value);
	endpoint_request(id, receive_payload, send_payload, 1, 4);
}


void ODrive::send_to_odrive(libusb_device_handle* handle, std::vector<uint8_t>& packet, int* sent_bytes) {
	libusb_bulk_transfer(handle,
		(1 | LIBUSB_ENDPOINT_OUT),
		packet.data(),
		packet.size(),
		sent_bytes,
		0);
}


void ODrive::receive_from_odrive(libusb_device_handle* handle, unsigned char* packet, int max_bytes_to_receive, int* received_bytes, int timeout) {
	libusb_bulk_transfer(handle,
		(1 | LIBUSB_ENDPOINT_IN),
		packet,
		max_bytes_to_receive,
		received_bytes,
		timeout);
}


void ODrive::serialize(serial_buffer& serial_buffer, const int& value) {
	serial_buffer.push_back((value >> 0) & 0xFF);
	serial_buffer.push_back((value >> 8) & 0xFF);
	serial_buffer.push_back((value >> 16) & 0xFF);
	serial_buffer.push_back((value >> 24) & 0xFF);
}


void ODrive::serialize(serial_buffer& serial_buffer, const float& valuef) {
	union {
		float f;
		int i;
	}u;
	u.f = valuef;
	serialize(serial_buffer, u.i);
}


void ODrive::serialize(serial_buffer& serial_buffer, const short& value) {
	serial_buffer.push_back((value >> 0) & 0xFF);
	serial_buffer.push_back((value >> 8) & 0xFF);
}


void ODrive::serialize(serial_buffer& serial_buffer, const std::vector<uint8_t>& value) {
	serial_buffer = value;
}


void ODrive::deserialize(serial_buffer::iterator& it, short& value) {
	value = *it++;
	value |= (*it++) << 8;
}


void ODrive::deserialize(serial_buffer::iterator& it, int& value) {
	value = *it++;
	value |= (*it++) << 8;
	value |= (*it++) << 16;
	value |= (*it++) << 24;
}


void ODrive::deserialize(serial_buffer::iterator& it, float& value) {
	union {
		float f;
		int i;
	}u;
	deserialize(it, u.i);
	value = u.f;
}


void ODrive::deserialize(serial_buffer::iterator& it, std::vector<uint8_t>& value) {
	value.push_back(1);
}


void ODrive::serialize(serial_buffer& serial_buffer, const odrive_packet& odrive_packet) {
	serialize(serial_buffer, odrive_packet.sequence_number, odrive_packet.endpoint_id, odrive_packet.payload_length, odrive_packet.payload, odrive_packet.CRC16);
}


void ODrive::deserialize(serial_buffer::iterator& it, odrive_packet& odrive_packet) {
	deserialize(it, odrive_packet.sequence_number, odrive_packet.endpoint_id, odrive_packet.payload_length, odrive_packet.payload, odrive_packet.CRC16);
}


serial_buffer ODrive::decode_odrive_packet(serial_buffer::iterator& it, short& seq_no, serial_buffer& received_packet) {
	serial_buffer payload;
	deserialize(it, seq_no);
	while (it != received_packet.end()) {
		payload.push_back(*it++);
	}
	return payload;
}
