require 'test/unit'
require 'xdr'

class TestXdr < Test::Unit::TestCase
  include XDR

  def good_test_cases(cases, packer, unpacker)
    cases.each do |c|
      self.send(unpacker, self.send(packer, c))
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
    good_test_cases([0, 1, -1, -9223372036854775808, 9223372036854775807], :pack_hyper, :unpack_uhyper)
    bad_test_cases([-9223372036854775809, 9223372036854775808], :pack_hyper, :unpack_uhyper)
  end

  def test_string
    good_test_cases(["", "foo\0bar", "the quick brown ..."], :pack_string, :unpack_string)
  end
end
