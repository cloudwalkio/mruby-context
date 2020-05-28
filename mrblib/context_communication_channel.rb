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
      ThreadScheduler.command(0, "app=#{application}")
      @app = application
    end

    def self.write(value)
      if Object.const_defined?(:Cloudwalk) && value.is_a?(Cloudwalk::HttpEvent)
        ThreadChannel.write(:send, value.message)
      else
        ThreadChannel.write(:send, value)
      end
    end

    def self.read
      ThreadChannel.read(:recv)
    end

    def self.code
      ThreadScheduler.command(1, "code", nil, true)
    end

    def self.close
      ThreadScheduler.cache_clear!
      ThreadScheduler.command(2, "close")
    end

    def self.connected?
      self.connection_cache do
        ThreadScheduler.command(3, "connected?")
      end
    end

    # TODO Scalone:
    # I decide to mock this call, it could be a problem on payment channel by
    # Websocket
    def self.handshake?
      true
    end

    def self.handshake
      ThreadScheduler.command(5, "handshake")
    end

    def self.connect(options = nil)
      self.connecting = true
      self.connecting_time = Time.now + 15
      if ThreadScheduler.command(6, "connect")
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
