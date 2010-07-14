require 'xdr'

module XDRUtils

  # The *_fn functions build unpacker functions

  # utility function to apply a series of unpackers
  def unpack_many_fn(*unpackers)
    lambda do |txt|
      results = Array.new
      unpackers.each do |u|
        v, txt = u.call(txt)
        results << v
      end

      [results, txt]
    end
  end
  
  def unpack_array_fn(n, u)
    unpack_many_fn(*Array.new(n, u))
  end

  def unpack_var_array_fn(u)
    lambda do |txt|
      n, txt = unpack_uint(txt)
      fn = unpack_array_fn(n, u)
      fn.call(txt)
    end
  end

  def unpack_opaque_fn(n)
    lambda do |txt|
      split_at(n, txt)
    end
  end

  def unpack_var_opaque_fn
    lambda do |txt|
      n, txt = unpack_uint(txt)
      fn = unpack_opaque_fn(n)
      fn.call(txt)
    end
  end

  def unpack_pointer_fn(u)
    lambda do |txt|
      b, txt = unpack_bool(txt)
      b ? u.call(txt) : [nil, txt]
    end
  end

  EnumDetail = Struct.new(:const, :symbol)

  def unpack_enum_fn(*details)
    lambda do |txt|
      n, txt = unpack_uint(txt)
      details.each do |d|
        if n == d.const
          return [d.symbol, txt]
        end
      end
      raise "unexpected enum constant (#{n})"
    end
  end

  FieldDetail = Struct.new(:unpacker, :name)

  def unpack_struct_fn(*fields)
    lambda do |txt|
      v = Message.new

      fields.each do |f|
        v[f.name], txt = f.unpacker.call(txt)
      end

      [v, txt]
    end
  end

  CaseDetail = Struct.new(:const, :unpacker, :name)

  def unpack_union_fn(discriminator, # FieldDetail
                      cases,         # [CaseDetail]
                      default)       # FieldDetail
    lambda do |txt|
      v = Message.new
      d, txt = discriminator.unpacker.call(txt)
      v[discriminator.name] = d
      cases.each do |c|
        if d == c.const
          v[c.name], txt = c.unpacker.call(txt)
          return [v, txt]
        end
      end

      if default
        v[default.name], txt = default.unpacker.call(txt)
        return [v, txt]
      end

      raise "invalid discriminator value"
    end
  end

  def unpack_void_fn
    lambda {|txt| [nil, txt]}
  end
end
