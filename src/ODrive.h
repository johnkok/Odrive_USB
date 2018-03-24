#pragma once
#include <string>
#include <vector>
#include <map>
#include "libusb-1.0\libusb.h"
#include <json.hpp>
#include "endpoint.h"

typedef std::vector<uint8_t> serial_buffer;
using nlohmann::json;

class Protocol {
public:

	Protocol(libusb_device_handle* handle) : handle(handle) {}

	int endpoint_request(int endpoint_id, serial_buffer& received_payload, std::vector<uint8_t> payload, int ack, int length);
	void get_json_interface(Endpoint& endpoints);

private:

	libusb_device_handle * handle;
	json j;

	void send_to_odrive(libusb_device_handle* handle, std::vector<uint8_t>& packet, int* sent_bytes) {
		libusb_bulk_transfer(handle,
			(1 | LIBUSB_ENDPOINT_OUT),
			packet.data(),
			packet.size(),
			sent_bytes,
			0);
	}

	void receive_from_odrive(libusb_device_handle* handle, unsigned char* packet, int max_bytes_to_receive, int* received_bytes, int timeout = 0) {
		libusb_bulk_transfer(handle,
			(1 | LIBUSB_ENDPOINT_IN),
			packet,
			max_bytes_to_receive,
			received_bytes,
			timeout);
	}

	// Templates for basic serialization and deserialization
	template <class T>
	void serialize(serial_buffer&, const T&);

	template <class T>
	void deserialize(serial_buffer::iterator&, T&);


	void serialize(serial_buffer& serial_buffer, const int& value) {
		serial_buffer.push_back((value >> 0) & 0xFF);
		serial_buffer.push_back((value >> 8) & 0xFF);
		serial_buffer.push_back((value >> 16) & 0xFF);
		serial_buffer.push_back((value >> 24) & 0xFF);
	}

	void serialize(serial_buffer& serial_buffer, const short& value) {
		serial_buffer.push_back((value >> 0) & 0xFF);
		serial_buffer.push_back((value >> 8) & 0xFF);

	}

	void serialize(serial_buffer& serial_buffer, const std::vector<uint8_t>& value) {
		serial_buffer = value;

	}

	void deserialize(serial_buffer::iterator& it, short& value) {
		value = *it++;
		value |= (*it++) << 8;
	}

	void deserialize(serial_buffer::iterator& it, int& value) {
		value = *it++;
		value |= (*it++) << 8;
		value |= (*it++) << 16;
		value |= (*it++) << 24;
	}

	void deserialize(serial_buffer::iterator& it, float& value) {
		union {
			float f;
			int temp;
		}u;
		int test = 0;
		test = *it++;
		test |= (*it++) << 8;
		test |= (*it++) << 16;
		test |= (*it++) << 24;
		u.temp = test;
		value = u.f;
	}

	void deserialize(serial_buffer::iterator& it, std::vector<uint8_t>& value) {
		value.push_back(1);
	}


	struct odrive_packet {
		short sequence_number;
		short endpoint_id;
		short payload_length;
		std::vector<uint8_t> payload;
		short CRC16;
	};


	// Variadic template to allow multiple arguments
	template <class T, class U, class ...Us>
	void serialize(serial_buffer& serial_buffer, const T& t, const U& u, const Us&... us) {
		serialize(serial_buffer, t);
		serialize(serial_buffer, u, us...);
	}


	template <class T, class U, class ...Us>
	void deserialize(serial_buffer::iterator& it, T& t, U& u, Us&... us) {
		deserialize(it, t);
		deserialize(it, u, us...);
	}


	// Struct serialization
	void serialize(serial_buffer& serial_buffer, const odrive_packet& odrive_packet) {
		serialize(serial_buffer, odrive_packet.sequence_number, odrive_packet.endpoint_id, odrive_packet.payload_length, odrive_packet.payload, odrive_packet.CRC16);
	}

	void deserialize(serial_buffer::iterator& it, odrive_packet& odrive_packet) {
		deserialize(it, odrive_packet.sequence_number, odrive_packet.endpoint_id, odrive_packet.payload_length, odrive_packet.payload, odrive_packet.CRC16);
	}

	template <class ...Ts>
	serial_buffer create_odrive_packet(short seq_no, int endpoint, short response_size, const Ts&... ts) {
		serial_buffer payload;
		serialize(payload, ts...);


		serial_buffer data;
		short crc = 0;
		if ((endpoint & 0x7fff) == 0) {
			crc = 1;
		}
		else {
			crc = 7230;
		}
		serialize(data, (short)seq_no, (short)endpoint, (short)response_size);

		for (uint8_t b : payload) {
			data.push_back(b);
		}

		serialize(data, (short)crc);

		return data;
	}

	serial_buffer decode_odrive_packet(serial_buffer::iterator& it, short& seq_no, serial_buffer& received_packet) {
		serial_buffer payload;
		deserialize(it, seq_no);

		// add this to a template?
		while (it != received_packet.end()) {
			payload.push_back(*it++);
		}
		return payload;
	}

	void parse_endpoints(Endpoint& endpoints, json& j) {
		for (json& obj : j) {
			std::string name = obj["name"];
			std::string type = obj["type"];
			int id = obj["id"];
			//printf("id: %i\tname: %s\ttype: %s\n", id, name.c_str(), type.c_str());

			endpoints.add_child(name, type, id);

			if (obj.count("members")) {
				json& members = obj["members"];
				parse_endpoints(endpoints[name], members);
			}
		}
	}


};