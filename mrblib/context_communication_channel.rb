class Context
  class CommunicationChannel
    THREAD_EXTERNAL_COMMUNICATION = :communication
    THREAD_INTERNAL_COMMUNICATION = 1

    class << self
      attr_accessor :boot_time, :booting, :connecting, :connecting_time, :app
    end

    self.boot_time = Time.now
    self.booting   = true

    def initialize(client = nil)
    end

    def self.booting?
      if self.booting
        if (self.boot_time + 180) < Time.now
          self.booting = false
        end
      end
      self.booting
    end

    def self.client
      true
    end

    def self.app=(application)
      ThreadScheduler.command(THREAD_EXTERNAL_COMMUNICATION, "app=#{application}")
      @app = application
    end

    def self.write(value)
      if Object.const_defined?(:Cloudwalk) && value.is_a?(Cloudwalk::HttpEvent)
        ThreadChannel.channel_write(THREAD_INTERNAL_COMMUNICATION, value.message)
      else
        ThreadChannel.channel_write(THREAD_INTERNAL_COMMUNICATION, value)
      end
    end

    def self.read
      ThreadChannel.channel_read(THREAD_INTERNAL_COMMUNICATION)
    end

    def self.code
      ThreadScheduler.command(THREAD_EXTERNAL_COMMUNICATION, "code")
    end

    def self.close
      ThreadScheduler.cache_clear!
      ThreadScheduler.command(THREAD_EXTERNAL_COMMUNICATION, "close")
    end

    def self.connected?
      self.connection_cache do
        ret = ThreadScheduler.command(THREAD_EXTERNAL_COMMUNICATION, "connected?")
        ret
      end
    end

    def self.handshake?
      self.connection_cache do
        if self.connected?
          timeout = Time.now + Device::Setting.tcp_recv_timeout.to_i
          loop do
            break(true) if ThreadScheduler.command(THREAD_EXTERNAL_COMMUNICATION, "handshake?")
            break if Time.now > timeout || getc(200) == Device::IO::CANCEL
          end
        end
      end
    end

    def self.handshake
      ThreadScheduler.command(THREAD_EXTERNAL_COMMUNICATION, "handshake")
    end

    def self.connect(options = nil)
      self.connecting = true
      self.connecting_time = Time.now + 15
      if ThreadScheduler.command(THREAD_EXTERNAL_COMMUNICATION, "connect")
        self
      end
    end

    def self.handshake_response
      if DaFunk::ParamsDat.file["access_token"]
        {"token" => DaFunk::ParamsDat.file["access_token"]}.to_json
      end
    end

    def self.check
      if Device::Network.connected? && self.connected? && self.handshake?
        self.read
      end
    end

    def self.connection_cache(&block)
      if self.connecting && self.connecting_time > Time.now
        true
      else
        self.connecting = false
        block.call
      end
    end
  end
end
