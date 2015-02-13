class Context
  def self.start(app = "base", platform = nil)
    begin
      $LOAD_PATH = ["./#{app}"]

      # Library responsable for common code and API syntax for the user
      if File.exist?("./#{app}/da_funk.mrb")
        require "da_funk.mrb"
      else
        require "./base/da_funk.mrb"
      end

      # Platform library responsible for implement the adapter for DaFunk
      # class Device #DaFunk abstraction
      #   self.adapter =
      if File.exist?("./base/#{platform}.mrb")
        require "./base/#{platform}.mrb"
        DaFunk.constantize(platform).setup
      else
        DaFunk.setup_command_line
      end

      require "main.mrb"

      # Main should have implement method call
      #  method call was need to avoid memory leak on irep table
      Main.call
    rescue => @exception
      Device::Display.clear if Device::Display.adapter.respond_to? :clear
      puts "#{@exception.class}: #{@exception.message}"
      puts "#{@exception.backtrace[0..2].join("\n")}"
      IO.getc
      return nil
    end
  end
end

