require 'test/unit'
require 'xdr'
require 'xdr_utils'

class TestXdr < Test::Unit::TestCase
  include XDR
  include XDRUtils

  def good_test_cases(cases, packer, unpacker)
    cases.each do |c|
      c2, txt = self.send(unpacker, self.send(packer, c))
      assert_equal(c, c2)
      assert_equal('', txt)
    end
  end

  def bad_test_cases(cases, packer, unpacker)
    cases.each do |c|
      assert_raises(RuntimeError) do
        self.send(unpacker, self.send(packer, c))
      end
    end
  end

  def test_uint
    good_test_cases([0, 1, 56, 4294967295], :pack_uint, :unpack_uint)
    bad_test_cases([-1, 4294967296], :pack_uint, :unpack_uint)
  end

  def test_int
    good_test_cases([0, -1, 1, -2147483648, 2147483647], :pack_int, :unpack_int)
    bad_test_cases([-2147483649,2147483648], :pack_int, :unpack_int)
  end

  def test_bool
    good_test_cases([true, false], :pack_bool, :unpack_bool)
  end

  def test_uhyper
    good_test_cases([0, 1, 4294967296, 18446744073709551615], :pack_uhyper, :unpack_uhyper)
    bad_test_cases([-1, 18446744073709551616], :pack_uhyper, :unpack_uhyper)
  end

  def test_hyper
    good_test_cases([0, 1, -1, -9223372036854775808, 9223372036854775807], :pack_hyper, :unpack_hyper)
    bad_test_cases([-9223372036854775809, 9223372036854775808], :pack_hyper, :unpack_hyper)
  end

  def test_string
    good_test_cases(["", "foo\0bar", "the quick brown ..."], :pack_string, :unpack_string)
  end

  def pack_test_enum(v)
    pack_enum({:one => 1, :two => 2, :three => 45}, v)
  end

  def unpack_test_enum(txt)
    unpack_enum({1 => :one, 2 => :two, 45 => :three}, txt)
  end

  # def test_enum
  #   good_test_cases([:one, :two, :three], :pack_test_enum, :unpack_test_enum)
  #   bad_test_cases([:foo, :blip, :onetwo, 'three'], :pack_test_enum, :unpack_test_enum)
  # end

  def test_array
    a = [1, 32, 63, 99, 1000]
    txt = pack_array(5, a) {|v| pack_uint(v)}

    fn = unpack_array_fn(a.length, lambda {|txt| unpack_uint(txt)})
    a2, txt = fn.call(txt)
    assert_equal(a, a2)
    assert_equal('', txt)

    assert_raises(RuntimeError) do
      pack_array(6, a) {|v| pack_uint(v)}
    end
  end

  def test_var_array
    a = [1, 32, 34, 45, 1231]
    txt = pack_uint(5) + (pack_array(5, a) {|v| pack_uint(v)})
    a2, txt = unpack_var_array_fn(lambda {|txt| unpack_uint(txt)}).call(txt)
    assert_equal(a, a2)
    assert_equal(txt, '')
  end

  def test_opaque
    txt = 'foobar'
    v, txt = unpack_opaque_fn(3).call(txt)
    assert_equal('foo', v)
    assert_equal('bar', txt)
  end

  def test_var_opaque
    txt = pack_uint(3) + 'foobar'
    v, txt = unpack_var_opaque_fn().call(txt)
    assert_equal('foo', v)
    assert_equal('bar', txt)
  end

  def test_pointer
    fn = unpack_pointer_fn(lambda {|x| unpack_uint(x)})

    txt = pack_bool(false)
    v, txt = fn.call(txt)

    assert_equal(nil, v)
    assert_equal('', txt)

    txt = pack_bool(true) + pack_uint(56)
    v, txt = fn.call(txt)

    assert_equal(56, v)
    assert_equal('', txt)
  end

  def test_enum_unpacker
    fn = unpack_enum_fn([EnumDetail.new(5, :red),
                         EnumDetail.new(6, :blue),
                         EnumDetail.new(45, :green)])

    v, txt = fn.call(pack_uint(5))
    assert_equal(:red, v)
    assert_equal('', txt)

    v, _ = fn.call(pack_uint(6))
    assert_equal(:blue, v)

    v, _ = fn.call(pack_uint(45))
    assert_equal(:green, v)
  end

  def test_unpack_struct
    txt = pack_uint(34) + pack_uint(45)

    fn = lambda {|v| unpack_uint(v)}
    unpacker = unpack_struct_fn(FieldDetail.new(fn, :v1),
                                FieldDetail.new(fn, :v2))

    r, _ = unpacker.call(txt)
    assert_equal(34, r.v1)
    assert_equal(45, r.v2)
  end

  def test_unpack_many
    txt = pack_uint(34) + pack_uint(45)

    fn = lambda {|v| unpack_uint(v)}
    unpacker = unpack_many_fn(fn, fn)

    rs, _ = unpacker.call(txt)
    assert_equal([34, 45], rs)
  end

  def test_unpack_union
    fn = unpack_union_fn(FieldDetail.new(lambda {|x| unpack_uint(x)},
                                         :discriminator),
                         [CaseDetail.new(1, lambda {|x| unpack_int(x)}, :f1),
                          CaseDetail.new(2, lambda {|x| unpack_string(x)}, :f2)],
                         FieldDetail.new(unpack_array_fn(2, lambda {|x| unpack_uint(x)}), :f3))

    v, txt = fn.call(pack_uint(1) + pack_int(7))
    assert_equal(1, v.discriminator)
    assert_equal(7, v.f1)
    assert_equal('', txt)

    v, _ = fn.call(pack_uint(2) + pack_string('hello, world!'))
    assert_equal(2, v.discriminator)
    assert_equal('hello, world!', v.f2)

    v, _ = fn.call(pack_uint(97) + pack_uint(5) + pack_uint(78))
    assert_equal(97, v.discriminator)
    assert_equal([5, 78], v.f3)
  end

  def test_unpack_void
    fn = unpack_void_fn
    v, txt = fn.call('foo')
    assert_equal(nil, v)
    assert_equal('foo', txt)
  end
end
