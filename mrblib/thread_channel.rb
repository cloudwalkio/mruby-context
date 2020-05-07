class Context
  class ThreadChannel
    class ChannelNotFound < StandardError; end

    CHANNEL_SEND          = :send
    CHANNEL_RECV          = :recv
    CHANNEL_INTERNAL_SEND = 0
    CHANNEL_INTERNAL_RECV = 1

    CHANNELS = {
      CHANNEL_SEND => CHANNEL_INTERNAL_SEND,
      CHANNEL_RECV => CHANNEL_INTERNAL_RECV
    }

    def self.id
      @id || 0
    end

    def self.channels=(value = {})
      @channels = value
    end

    def self.channels
      CHANNELS || @channels
    end

    def self.internal_channel(channel)
      value = channels[channel]
      raise ChannelNotFound.new("channel #{channel.inspect} not found") unless value
      value
    end

    def self.generate_id
      if Context::ThreadScheduler.communication_thread?
        id
      else
        @id = Time.now.to_i + rand(99999)
      end
    end

    def self.write(channel, buf, event_id = nil)
      _write(1, internal_channel(channel), event_id || generate_id, buf)
    end

    def self.read(channel, event_id = id)
      @id, buf = _read(1, internal_channel(channel), id)
      buf
    end

    # main thread
    # -> write(:send, '1234')
    #   => generate event id 1
    #
    # communication thread
    # -> read(:send, 0)
    #   => send event id 0 to get first
    #   => read event id 1
    #   => '5678'
    #
    # -> communicate in the thread
    #
    # -> write(:recv, '5678')
    #   => id 1
    #
    # main thread
    # -> read(:recv, 1)
    #   => id 1
    #   => '5678'
    #
  end
end
