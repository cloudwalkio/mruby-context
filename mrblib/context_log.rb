class ContextLog
  class << self
    attr_accessor :enable
    alias_method :enable?, :enable
  end
  self.enable = true

  FILE_LOG = "./main/main.log"

  def self.error(exception, backtrace, text = "")
    persist do |handle|
      handle.write("\n========================================")
      unless text.empty?
        handle.write("\n#{text}")
        handle.write("\n----------------------------------------")
      end
      handle.write("\n#{exception.class}: #{exception.message}")
      handle.write("\n#{backtrace.join("\n")}")
      handle.write("\n========================================")
    end
  end

  def self.info(text = "")
    persist do |handle|
      handle.write("\n#{self.time} - INFO - [#{text}]")
    end
  end

  def self.warn(text = "")
    persist do |handle|
      handle.write("\n#{self.time} - WARN - [#{text}]")
    end
  end

  def self.persist
    return unless self.enable?
    File.open(FILE_LOG, "a") { |handle| yield(handle) if block_given? }
  end

  def self.enable?
    self.enable
  end

  def self.time
    time = Time.now
    "%d-%d-%d %02d:%02d:%02d:%06d" % [time.year, time.month, time.day, time.hour, time.min, time.sec, time.usec]
  end
end
