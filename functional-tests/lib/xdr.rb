require 'message'

module XDR
  def bounds_check(n, low, high)
    raise "out of bounds" if n < low || n > high
  end

  # returns the padding needed at the end of some data of length |n|
  def padding(n, c = "\0")
    m = n % 4
    c * (m == 0 ? 0 : 4 - m)
  end

  def drop(n, txt)
    txt[n..-1]
  end

  def split_at(n, txt)
    [txt[0..(n - 1)], txt[n..-1]]
  end

  def not_implemented
    raise "not implemented"
  end

  # A serialiser implements the following 2 methods
  def pack_uint(n)
    bounds_check(n, 0, 4294967295)
    [n].pack('N')
  end

  def unpack_uint(txt)
    [txt.unpack('N')[0], drop(4, txt)]
  end

  def pack_int(n)
    pack_uint(n + 2147483648)
  end

  def unpack_int(txt)
    [unpack_uint(txt)[0] - 2147483648, drop(4, txt)]
  end

  def pack_bool(b)
    [b ? 1 : 0].pack('N')
  end

  def unpack_bool(txt)
    [txt.unpack('N')[0] == 1, drop(4, txt)]
  end

  def pack_uhyper(n)
    bounds_check(n, 0, 18446744073709551615)
    l = n & 0xffffffff
    h = n >> 32
    [h, l].pack('NN')
  end

  def unpack_uhyper(txt)
    h, l = txt.unpack('NN')
    [(h << 32) | l, drop(8, txt)]
  end

  def pack_hyper(n)
    pack_uhyper(n + 9223372036854775808)
  end

  def unpack_hyper(txt)
    v, txt = unpack_uhyper(txt)
    [v - 9223372036854775808, txt]
  end

  def pack_float(f)
    not_implemented
  end

  def unpack_float(txt)
    not_implemented
  end

  def pack_double(d)
    not_implemented
  end

  def unpack_double(txt)
    not_implemented
  end

  # useful for implementing the opaque and string packers
  def pack_raw(str)
    str + padding(str.length)
  end

  def unpack_raw(len, txt)
    v, txt = split_at(len, txt)
    [v, drop(padding(len).length, txt)]
  end

  def pack_string(str)
    len = str.length
    pack_uint(len) + pack_raw(str)
  end

  def unpack_string(txt)
    len, txt = unpack_uint(txt)
    unpack_raw(len, txt)
  end

  def pack_enum(enum_hash, v)
    if enum_hash.member?(v)
      pack_uint(enum_hash[v])
    else
      raise "invalid enum value (#{v})"
    end
  end

  def unpack_enum(enum_hash, txt)
    v, txt = unpack_uint(txt)
    if enum_hash.member?(v)
      [enum_hash[v], txt]
    else
      raise "invalid enum value (#{v})"
    end
  end

  def pack_array(n, array)
    if array.length != n
      raise "incorrect array size, was #{array.length}, expected #{n}"
    end
    (0..array.length - 1).map do |i|
      yield(array[i])
    end.join
  end

  def unpack_array(n, txt)
    v = Array.new
    (0..n - 1).each do |i|
      v[i], txt = yield(txt)
    end
    [v, txt]
  end

  # utility function to apply a series of unpackers
  def unpack_many(*unpackers)
    lambda do |txt|
      results = Array.new
      unpackers.each do |u|
        v, txt = u.call(txt)
        results << v
      end

      [results, txt]
    end
  end
  
  def unpack_array(n, u)
    unpack_many(Array.new(n, u))
  end

  def unpack_var_array(u)
    lambda do |txt|
      n, txt = unpack_uint(txt)
      fn = unpack_array(n, u)
      fn.call(txt)
    end
  end

  def unpack_opaque(n)
    lambda do |txt|
      split_at(n, txt)
    end
  end

  def unpack_var_opaque
    lambda do |txt|
      n, txt = unpack_uint(txt)
      fn = unpack_opaque(n)
      fn.call(txt)
    end
  end

  def unpack_pointer(u)
    lambda do |txt|
      b, txt = unpack_bool(txt)
      b ? u.call(txt) : [nil, txt]
    end
  end

  EnumDetail = Struct.new(:const, :symbol)

  def unpack_enum(*details)
    lambda do |txt|
      n, txt = unpack_uint(txt)
      details.each do |d|
        if n == d.const
          return d.symbol
        end
      end
      raise "unexpected enum constant (#{n})"
    end
  end

  FieldDetail = Struct.new(:unpacker, :name)

  def unpack_struct(*fields)
    lambda do |txt|
      v = Message.new

      fields.each do |f|
        v[f.name], txt = f.unpacker.call(txt)
      end

      [v, txt]
    end
  end

  CaseDetail = Struct.new(:const, :unpacker, :name)

  def unpack_union(/* FieldDetail */ discriminator,
                   /* CaseDetail* */ cases,
                   /* FieldDetail */ default)
    lambda do |txt|
      v = Message.new
      d = discriminator.unpacker.call(txt)
      v.[discriminator.name] = d
      cases.each do |c|
        if d == c.const
          v.[c.name], txt = c.unpacker.call(txt)
          return [v, txt]
        end
      end

      if default
        v.[default.name], txt = default.unpacker.call(txt)
        return [v, txt]
      end

      raise "invalid discriminator value"
    end
  end

  def unpack_void
    lambda {|txt| [nil, txt]}
  end
end
