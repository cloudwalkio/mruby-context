class Context
  class CommunicationChannel
    THREAD_COMMUNICATION = 1

    def initialize(client = nil)
    end

    def self.client
      true
    end

    def self.write(value)
      ThreadChannel.channel_write(THREAD_COMMUNICATION, value)
    end

    def self.read
      ThreadChannel.channel_read(THREAD_COMMUNICATION)
    end

    def self.close
      ThreadScheduler.command(THREAD_COMMUNICATION, "close")
      PaymentChannel.client = nil
    end

    def self.connected?
      ThreadScheduler.command(THREAD_COMMUNICATION, "connected?")
    end

    def self.handshake?
      if self.connected?
        timeout = Time.now + Device::Setting.tcp_recv_timeout.to_i
        loop do
          break(true) if ThreadScheduler.command(THREAD_COMMUNICATION, "handshake?")
          break if Time.now > timeout || getc(200) == Device::IO::CANCEL
        end
      end
    end
    end

    def self.check
      if Device::Network.connected? && self.connected? && self.handshake?
        message = self.read
      end
      if message.nil? && DaFunk::ConnectionManagement.primary_try?
        return :primary_communication
      end
      message
    end
  end
end
