class Context
  ENV_PRODUCTION  = "production"
  ENV_DEVELOPMENT = "development"

  class << self
    attr_accessor :env
  end
  self.env = ENV_DEVELOPMENT

  def self.execute(app = "main", platform = nil, json = nil)
    if app.split(".").last == "posxml"
      posxml(app, platform, json)
    else
      start(app, platform, json)
      ruby(app, platform, json, true)
    end
  rescue => exception
    self.treat(exception)
  ensure
    self.teardown
  end

  def self.start(app = "main", platform = nil, json = nil)
    ruby(app, platform, json, false)
  rescue => exception
    self.treat(exception)
  ensure
    self.teardown
  end

  def self.posxml(file, platform, json = nil)
    $LOAD_PATH.unshift "./main"
    self.setup(file, platform)
    Device::System.klass = file
    PosxmlInterpreter.new(file, json).start
  end

  def self.ruby(app, platform, json, exec = true)
    unless exec
      $LOAD_PATH = ["./#{app}"]

      if Object.const_defined? :Platform
        Platform.boot if Platform.respond_to?(:boot)
      end

      self.setup(app, platform)
      Device::System.klass = app if require "main.mrb"
    else
      # Necessary to send information to communication class
      Device::System.klass = app
      Device::Runtime.system_reload
      # Main should have implement method call
      #  method call was need to avoid memory leak on irep table
      begin
        Main.call(json)
      rescue ArgumentError
        Main.call
      end
    end
  end

  def self.setup(app, platform)
    # Library responsable for common code and API syntax for the user
    if File.exist?("./#{app}/da_funk.mrb")
      require "da_funk.mrb"
    else
      require "./main/da_funk.mrb"
    end

    # Platform library responsible for implement the adapter for DaFunk
    # class Device #DaFunk abstraction
    #   self.adapter =
    platform_mrb = "./main/#{platform.to_s.downcase}.mrb"
    if platform && File.exist?(platform_mrb)
      require platform_mrb
      Device::Support.constantize(platform).setup
    else
      require "./main/command_line_platform.mrb"
      # TODO
      # DaFunk.setup_command_line
    end
    DaFunk::PaymentChannel.client = Context::CommunicationChannel
    Device::Runtime.system_reload
  end

  def self.teardown
    if Object.const_defined?(:Device) && Device.const_defined?(:System) && Device::System.respond_to?(:teardown)
      Device::System.teardown
    end
  end

  def self.treat(exception, message = "")
    backtrace = exception.backtrace
    ContextLog.exception(exception, backtrace, message)
    Device::Display.clear if self.clear_defined?
    if self.development?
      puts "#{exception.class}: #{exception.message}"
      puts backtrace[0..2].join("\n")
    else
      puts "\nOOOOPS!"
      puts "UNEXPECTED ERROR"
      puts "CONTACT THE ADMINISTRATOR, PROBLEM LOGGED WITH SUCCESS."
    end
    getc(5000)
  end

  def self.clear_defined?
    Object.const_defined?(:Device) && Device.const_defined?(:Display) && Device::Display.adapter.respond_to?(:clear)
  end

  def self.development?
    self.env == ENV_DEVELOPMENT
  end

  def self.production?
    self.env == ENV_PRODUCTION
  end
end

