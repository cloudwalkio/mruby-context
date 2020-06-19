class Context
  class ThreadPubSub
    def self.publish(text, avoid_id = nil)
      _publish(text, avoid_id || @subscribed_id)
    end

    def self.listen(id)
      _listen(id)
    end

    def self.subscribe
      @subscribed_id = _subscribe
    end
  end
end
