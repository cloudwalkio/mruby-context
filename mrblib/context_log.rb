class ContextLog
  class << self
    attr_accessor :enable, :adapter
    alias_method :enable?, :enable
    attr_accessor :file_log
  end
  self.enable = true

  FILE_LOG = "./main/main.log"

  def self.exception(exception, backtrace, text = "")
    persist do |handle|
      unless text.empty?
        handle.write("\n#{self.time(true)} - EXCEPTION - [[EX] TEXT] #{text}]")
      end
      handle.write("\n#{self.time(true)} - EXCEPTION - [[EX] CLASS] #{exception.class}: #{exception.message}]")
      handle.write("\n#{self.time(true)} - EXCEPTION - [[EX] BACKTRACE] #{backtrace.join("\n")}]")
    end
  end

  def self.error(text = "", utc = false)
    persist do |handle|
      handle.write("\n#{self.time(utc)} - ERROR - [#{text}]")
    end
  end

  def self.info(text = "", utc = false)
    persist do |handle|
      handle.write("\n#{self.time(utc)} - INFO - [#{text}]")
    end
  end

  def self.warn(text = "", utc = false)
    persist do |handle|
      handle.write("\n#{self.time(utc)} - WARN - [#{text}]")
    end
  end

  def self.persist
    return unless self.enable?
    file = self.file_log || FILE_LOG
    path = file.gsub("/main.log", "/#{self.time_path}.log")
    File.open(path, "a") do |handle|
      if block_given?
        yield(self.adapter) if self.adapter
        yield(handle)
      end
    end
  end

  def self.enable?
    self.enable
  end

  def self.time_path
    time = Time.now
    "%d-%02d-%02d" % [time.year, time.month, time.day]
  end

  def self.time(utc)
    time = Time.now
    if utc
      if Device::Setting.cw_pos_timezone.nil? || Device::Setting.cw_pos_timezone.empty?
        # by default we assume that we're using brasilia timezone
        time += 10800
      elsif Device::Setting.cw_pos_timezone[0] == '+'
        time += ((Device::Setting.cw_pos_timezone[1..2].to_i) * 60 * 60)
      else
        time -= ((Device::Setting.cw_pos_timezone[1..2].to_i) * 60 * 60)
      end
    end
    "%d-%02d-%02d %02d:%02d:%02d:%06d" % [time.year, time.month, time.day, time.hour, time.min, time.sec, time.usec]
  end
end
