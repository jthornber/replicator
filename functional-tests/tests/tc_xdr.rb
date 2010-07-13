require 'test/unit'
require 'xdr'

class TestXdr < Test::Unit::TestCase
  include XDR

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

  def test_enum
    good_test_cases([:one, :two, :three], :pack_test_enum, :unpack_test_enum)
    bad_test_cases([:foo, :blip, :onetwo, 'three'], :pack_test_enum, :unpack_test_enum)
  end

  def test_array
    a = [1, 32, 63, 99, 1000]
    txt = pack_array(a) {|v| pack_uint(v)}
    a2, txt = unpack_array(a.length, txt) {|txt| unpack_uint(txt)}
    assert_equal(a, a2)
    assert_equal('', txt)
  end
end
