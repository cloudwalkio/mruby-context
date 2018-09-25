class JsonLog
  def self.log_txt_to_json(logname)
    result = {
      :connections => [],
      :errors => [],
      :warnings => [],
      :general => []
    }

    array = []
    File.open("./main/#{logname}").each_line do |line|
      array.push(line) if self.log_line_ok?(line)
      if (line.index('D') && line.index("finished")) || (line.index('T') && line.index("WebSocket Recv Size"))
        result[:connections].push(self.parse_log(array))
        array = []
      elsif line.index("ERROR") || line.index("EXCEPTION")
        result[:errors].push(self.parse_log(array))
        array = []
      elsif line.index("WARN")
        result[:warnings].push(self.parse_log(array))
        array = []
      end
    end
    result[:general].push(self.add_injected_keys_on_json_log)
    result
  end

  private

  def self.log_line_ok?(line)
    ["[EX]", "[C]", "[D]", "[T]", "[F]"].each do |label|
      return true if line.include?(label)
    end
    return false
  end

  def self.parse_log(array)
    details = {
      :type => " ",
      :events => []
    }
    array.each do |str|
      hash = {}
      timestamp = self.get_timestamp(str)
      event = str[(str.index('[') + 1)..-3]
      details[:type] = self.get_type_transaction(event)

      hash[:timestamp] = timestamp
      hash[:event] = event
      details[:events].push(hash)
    end
    details
  end

  def self.get_type_transaction(event)
    if event.index('[D]')
      return "download"
    elsif event.index('[F]') || event.index('[T]')
      return "financial"
    elsif event.index('[E]')
      return "error"
    elsif event.index('[EX]')
      return "exception"
    else
      return "unknown"
    end
  end

  def self.get_timestamp(str)
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

  def self.add_injected_keys_on_json_log
    keys = { :injected_keys => { :dukpt => [], :ms => [] } }
    self.add_dukpt_keys(keys)
    self.add_ms_keys(keys)
    keys
  end

  def self.add_dukpt_keys(keys)
    (0..99).inject({}) do |hash, slot|
      key = Device::Pinpad.key_ksn(slot)
      if key[:pin][0] == 0
        hash[:slot] = slot
        hash[:ksi] = key[:pin][1]
        keys[:injected_keys][:dukpt].push(hash)
        hash = {}
      end
      hash
    end
  end

  def self.add_ms_keys(keys)
    (0..99).inject({}) do |hash, slot|
      key = Device::Pinpad.key_kcv(slot)
      if key[:ms3des][0] == 0
        hash[:slot] = slot
        hash[:kcv] = key[:ms3des][1]
        keys[:injected_keys][:ms].push(hash)
        hash = {}
      end
      hash
    end
  end
end
