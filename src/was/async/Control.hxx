// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/net/BufferedSocket.hxx"
#include "DefaultFifoBuffer.hxx"

#include <was/protocol.h>

#include <cstddef>
#include <span>
#include <string_view>

namespace Was {

class ControlHandler {
public:
	/**
	 * A packet was received.
	 *
	 * @return false if the object was closed
	 */
	virtual bool OnWasControlPacket(enum was_command cmd,
					std::span<const std::byte> payload) noexcept = 0;

	/**
	 * Called after a group of control packets have been handled, and
	 * the input buffer is drained.
	 *
	 * @return false if the #WasControl object has been destructed
	 */
	virtual bool OnWasControlDrained() noexcept {
		return true;
	}

	virtual void OnWasControlDone() noexcept = 0;
	virtual void OnWasControlHangup() noexcept = 0;
	virtual void OnWasControlError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Web Application Socket protocol, control channel library.
 *
 * This class does not "own" the socket and its destructor does not
 * close the socket.  To do that manually, call the Close() method.
 */
class Control final : BufferedSocketHandler {
	static constexpr Event::Duration write_timeout = std::chrono::minutes{1};

	BufferedSocket socket;

	bool done = false;

	ControlHandler &handler;

	DefaultFifoBuffer output_buffer;

public:
	Control(EventLoop &event_loop, SocketDescriptor _fd,
		ControlHandler &_handler) noexcept;

	auto &GetEventLoop() const noexcept {
		return socket.GetEventLoop();
	}

	bool IsDefined() const noexcept {
		return socket.IsValid();
	}

	void Close() noexcept {
		if (socket.IsValid()) {
			socket.Close();
			socket.Destroy();
		}
	}

	/**
	 * Flush the output buffer now.
	 *
	 * @return true if all data has been sent successfully and the
	 * output buffer is empty, false if
	 * ControlHandler::OnWasControlError() has been called
	 */
	bool FlushOutput() noexcept;

	bool Send(enum was_command cmd,
		  std::span<const std::byte> payload={}) noexcept;

	bool SendString(enum was_command cmd,
			std::string_view payload) noexcept;

	/**
	 * Send a name-value pair (e.g. for #WAS_COMMAND_HEADER and
	 * #WAS_COMMAND_PARAMETER).
	 */
	bool SendPair(enum was_command cmd, std::string_view name,
		      std::string_view value) noexcept;

	bool SendT(enum was_command cmd, const auto &payload) noexcept {
		return Send(cmd, std::as_bytes(std::span{&payload, 1}));
	}

	bool SendUint64(enum was_command cmd, uint64_t payload) noexcept {
		return SendT(cmd, payload);
	}

	bool SendArray(enum was_command cmd,
		       std::span<const char *const> values) noexcept;

	void Done() noexcept;

	bool empty() const {
		return socket.IsEmpty() && output_buffer.empty();
	}

private:
	[[nodiscard]]
	std::byte *Start(enum was_command cmd, size_t payload_length) noexcept;
	void Finish(size_t payload_length) noexcept;

	void ScheduleWrite() noexcept;

public:
	/**
	 * Release the socket held by this object.
	 */
	void ReleaseSocket() noexcept;

private:
	void InvokeDone() noexcept {
		handler.OnWasControlDone();
	}

	void InvokeError(std::exception_ptr ep) noexcept {
		handler.OnWasControlError(ep);
	}

	void InvokeError(const char *msg) noexcept;

	bool InvokeDrained() noexcept {
		return handler.OnWasControlDrained();
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	// TODO: implement OnBufferedHangup()?
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	bool OnBufferedDrained() noexcept override;
	[[noreturn]]
	enum write_result OnBufferedBroken() override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

} // namespace Was
