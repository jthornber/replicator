require 'test/unit'
require 'protocol'
require 'replicator'

class TestLogon < Test::Unit::TestCase
  include Builders

  MAJOR = 1
  MINOR = 1
  PATCH = 1

  def do_logon(major, minor, patch, expect_success)
    @replicator = Replicator.new('127.0.0.1', 6776)
    req_id = @replicator.put_request(mk_logon(major, minor, patch))
    sleep(10)
    resp = @replicator.get_response(req_id)
    assert_equal(expect_success ? :SUCCESS : :FAIL, resp.discriminator)

    STDERR.puts "logon passed, now shutting down"
    @replicator.shutdown
  end

  def test_logon
    do_logon(MAJOR, MINOR, PATCH, true)
    do_logon(MAJOR, MINOR, PATCH - 1, true)
    do_logon(MAJOR, MINOR, PATCH + 1, true)

    do_logon(MAJOR, MINOR - 1, PATCH, true)
    do_logon(MAJOR, MINOR + 1, PATCH, false)

    do_logon(MAJOR + 1, MINOR, PATCH, false)
    do_logon(MAJOR - 1, MINOR, PATCH, true)
  end
end

