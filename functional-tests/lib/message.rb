class Message
  def initialize(fields = nil)
    @fields = fields.nil? ? Hash.new : fields
  end

  def []=(key, value)
    @fields[key] = value
  end

  def [](key)
    @fields.has_key?(key) ? @fields[key] : nil
  end

  private
  def check_arg_count(sym, count, args)
    if args.length != count
      raise "bad number of aruments for '#{sym}', expected #{count} but got #{args.length}"
    end
  end

  def method_missing(symbol, *args)
    str = symbol.id2name

    # is it an assignment ?
    if str =~ /(.*)=$/ then
      check_arg_count(symbol, 1, args)
      @fields[$1.to_sym] = args[0]
    else
      check_arg_count(symbol, 0, args)
      @fields[symbol]
    end
  end
end
