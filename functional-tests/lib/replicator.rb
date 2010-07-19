require 'protocol'
require 'socket'
require 'thread'
require 'pp'

class ReplicatorLog
  def initialize(log_file)
    @incoming = Queue.new
    @lines = Array.new
    @tid = reader_thread(log_file)
  end

  def shutdown()
    Thread.kill(@tid)
    while (@tid.alive?)
      sleep(0.01)
    end
  end

  def start_of_file()
    0
  end

  def mark()
    # make sure we're up to date
    @incoming.size.times do
      next_line()
    end

    mark_()
  end

  def get_txt(m1, m2 = nil)
    m2 = mark() if m2.nil?
    @lines[m1..m2]
  end

  def capture_log()
    m = @log.mark();
    r = yield
    [@log.get_txt(m), r]
  end

  def wait_for_event(event)
    loop do
      l = next_line()
      STDERR.puts "waiting for event: #{l}"
      if l =~ /EVENT \[([^\]]*)\] (.*)/
        if event == $1
          return mark_()
        end
      end
    end
  end

  private
  def mark_()
    @lines.size
  end

  def next_line()
    l = @incoming.pop()
    @lines << l
    l
  end

  # waits for the file in question to appear
  def self.open_log(file)
    loop do
      begin
        STDERR.puts "trying to open log"
        io = File.new(file)
        return io
      rescue
      end
      sleep(0.1)
    end
  end

  def reader_thread(file)
    io = ReplicatorLog.open_log(file)
    STDERR.puts "log opened"

    Thread.new(io, @incoming) do |file, incoming|
      begin
        while line = io.readline
          incoming.push(line)
        end
      rescue
        STDERR.puts "log reader thread threw exception #{pp(e)}"
      end

      STDERR.puts "log reader thread exiting"
    end
  end
end

class Replicator
  attr_reader :log

  def initialize(host, port)
    @response_queue = Queue.new
    @responses = Hash.new
    @request_id = 0

    # FIXME: hack
    File.unlink('log.log')

    # start the replicator
    @replicator_pid = spawn("#{ENV['REPLICATOR_PREFIX']} bin/replicator")

    # set up the log follower
    @log = ReplicatorLog.new("log.log") # FIXME: hardcoded log file
    @log.wait_for_event("SERVER_STARTED")
    STDERR.puts("replicator started (pid = #{@replicator_pid})")

    # connect to it
    begin
      @socket = TCPSocket.new(host, port)
    rescue
      shutdown_replicator
      raise
    end

    # kick off the thread that reads the responses
    @reader = run_reader_thread
  end

  def next_request_id
    r = @request_id
    @request_id = @request_id + 1
    r
  end

  def put_request(cmd)
    txt = pack_command(cmd)
    header = Message.new(:msg_size => txt.length,
                         :request_id => next_request_id())

    @socket.write(pack_msg_header(header))
    @socket.write(txt)

    header.request_id
  end

  def get_response(req_id)
    while !@responses.member?(req_id)
      wait_for_responses()
    end

    @responses[req_id]
  end

  def shutdown
    Thread.kill(@reader)
    while (@reader.alive?)
      sleep(0.01)
    end

    shutdown_replicator
  end

  private
  def shutdown_replicator
    Process::kill(:INT, @replicator_pid)
    Process::waitpid2(@replicator_pid)
  end

  def wait_(non_blocking)
    pair = @response_queue.pop(non_blocking)
    @responses[pair[0]] = pair[1]
  end

  def wait_for_responses
    wait_(false)                # a blocking wait
    count = 1

    # there may be more than one response ready (check with the
    # non-blocking flag set)
    @response_queue.size.times do
      wait_(true)
      count = count + 1
    end

    count
  end

  def self.read_response(sock)
    txt = ''
    begin
      txt = sock.read(8)
    rescue
      STDERR.puts "socket read failed"
      raise
    end

    STDERR.puts "read header"
    header, _ = unpack_msg_header(txt)
    STDERR.puts "unpacked header #{header}"
    begin
      STDERR.puts "now trying to read #{header.msg_size} bytes"
      txt = sock.read(header.msg_size)
    rescue
      STDERR.puts "socket read failed"
      raise
    end

    STDERR.puts "read response"
    resp, _ = unpack_response(txt)

    STDERR.puts "unpacked response"
    [header.request_id, resp]
  end

  def run_reader_thread
    Thread.new(@socket, @response_queue) do |sock, q|
      begin
        STDERR.puts "running thread"
        pair = Replicator.read_response(sock)
        STDERR.puts "read response"
        q.push(pair)
        STDERR.puts "pushed response"
      rescue e
        STDERR.puts "reader thread dying"
        pp e
        raise
      end
    end
  end
end

# These methods construct protocol message
module Builders
  def mk_logon(major, minor, patch)
    cmd = Message.new(:discriminator => :LOGON,
                      :logon => Message.new(:major => major,
                                            :minor => minor,
                                            :patch => patch))
  end
end
