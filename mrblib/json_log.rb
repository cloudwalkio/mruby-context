class JsonLog
  @result = {
    :connections => Array.new
  }

  def self.log_txt_to_json(logname)
    array = []
    File.read(logname).each_line do |line|
      array.push(line)
      if (line.index('D') && line.index("finished")) || (line.index('T') && line.index("WebSocket Recv Size")) ||
      line.index("ERROR") || line.index("WARN") || line.index("[EX] BACKTRACE")
        hash[:connections].push(parse_log(array))
        array = []
      end
    end
  end

  private

  def parse_log(array)
    details = {
      :type => " ",
      :events => Array.new
    }
    array.each do |str|
      hash = {}

      timestamp = get_timestamp(str)
      event = str[(str.index('[') + 1)..-3]
      details[:type] = get_type_transaction(event)

      hash[:timestamp] = timestamp
      hash[:event] = event
      details[:events].push(hash)
    end
    details
  end

  def get_type_transaction(event)
    if event.index('[D]')
      return "download"
    elsif event.index('[F]') || event.index('[T]')
      return "financial"
    elsif event.index('[EX')
      return "exception"
    else
      return "unknown"
    end
  end

  def get_timestamp(str)
    if str.index("ERROR")
      substring = "ERROR"
    elsif str.index("WARN")
      substring = "WARN"
    elsif str.index("EXCEPTION")
      substring = "EXCEPTION"
    else
      substring = "INFO"
    end
    str[0..(str.index(substring) - 4)]
  end
end
