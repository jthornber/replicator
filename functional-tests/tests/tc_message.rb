require 'test/unit'
require 'message'

class TestMessage < Test::Unit::TestCase
  def test_setting_fields
    msg = Message.new
    assert_nil(msg.foo)
    msg.foo = 'bar'
    assert_equal('bar', msg.foo)
    msg.x = 123
    assert_equal('bar', msg.foo)
    assert_equal(123, msg.x)
  end

  def test_init_list
    msg = Message.new(:foo => 'bar', :x => 123)
    assert_equal('bar', msg.foo)
    assert_equal(123, msg.x)
  end
end
