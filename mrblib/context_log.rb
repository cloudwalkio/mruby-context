class ContextLog
  class << self
    attr_accessor :enable
    alias_method :enable?, :enable
  end
  self.enable = true

  FILE_ERROR_LOG = "./main/error.log"
  FILE_INFO_LOG  = "./main/info.log"
  FILE_WARN_LOG  = "./main/warn.log"

  def self.error(exception, text = "")
    persist(FILE_ERROR_LOG) do |handle|
      handle.write("\n#{exception.class}: #{exception.message}")
      handle.write("\n#{exception.backtrace.join("\n")}")
      unless text.empty?
        handle.write("\n----------------------------------------")
        handle.write("\n#{text}")
        handle.write("\n----------------------------------------")
      end
    end
  end

  def self.info(text = "")
    persist(FILE_INFO_LOG) { |handle| handle.write(text) }
  end

  def self.warn(text = "")
    persist(FILE_WARN_LOG) { |handle| handle.write(text) }
  end

  def self.persist(filename)
    return unless self.enable?
    handle = File.open(filename, "a")
    handle.write("\n========================================")
    handle.write("\n#{Time.now}")

    yield(handle) if block_given?

    handle.write("\n========================================")
    handle.close unless handle.closed?
  end

  def self.enable?
    self.enable
  end
end
