require 'test/unit'
#require 'open3'
require 'protocol'

# FIXME: move to a library

class TestJournal < Test::Unit::TestCase
  def test_nothing
    assert_equal(1, 1)
  end

  def test_protocol_device_binding
    binding = Message.new
    binding.shortname = 1
    binding.logical_name = 'device 1'
    binding.path = '/dev/disc/foo'

    txt = pack_device_binding(binding)
    b2, _ = unpack_device_binding(txt)

    assert_equal(binding, b2)
  end

  def test_response
    resp = Message.new(:discriminator => :SUCCESS)
    txt = pack_response(resp)
    r2, _ = unpack_response(txt)
    assert_equal(resp, r2)
  end

  # def test_startup
  #   exe = 'bin/replicator'
  #   pid = Open3.popen3(exe) do |stdin, stdout, stderr|
      
  #   end

  #   puts pp
  # end
end

