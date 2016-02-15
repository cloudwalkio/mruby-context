class Context
  ENV_PRODUCTION  = "production"
  ENV_DEVELOPMENT = "development"

  class << self
    attr_accessor :env
  end
  self.env = ENV_DEVELOPMENT

  def self.start(app = "main", platform = nil, json = nil)
    begin
      if app.split(".").last == "posxml"
        posxml(app, platform)
      else
        ruby(app, platform, json)
      end
    rescue => @exception
      self.treat(@exception)
    end
  end

  def self.posxml(file, platform)
    $LOAD_PATH.unshift "./main"
    self.setup(file, platform)
    Device::System.klass = file
    PosxmlInterpreter.new(file).start
  end

  def self.ruby(app, platform, json)
    $LOAD_PATH = ["./#{app}"]
    self.setup(app, platform)
    Device::System.klass = app if require "main.mrb"

    # Main should have implement method call
    #  method call was need to avoid memory leak on irep table
    if json.nil? || json.empty?
      Main.call
    else
      Main.call(json)
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
  end

  def self.treat(exception, message = "")
    ContextLog.error(@exception, message)
    Device::Display.clear if self.clear_defined?
    if self.development?
      puts "#{@exception.class}: #{@exception.message}"
      puts @exception.backtrace[0]
    else
      puts "\nOooops!"
      puts "Unexpected error"
      puts "Contact the administrator, problem logged with success."
    end
    getc(0)
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

