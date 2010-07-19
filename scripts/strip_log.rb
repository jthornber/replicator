#!/usr/bin/env ruby1.9

# Strips the timestamps off logs like these

# 2010/07/19 12:53:43 DEBUG src/log/test/log_t.c:7 hello, world!
# 2010/07/19 12:53:43 INFO sldfkjsl
# 2010/07/19 12:53:43 WARN lskdjfls
# 2010/07/19 12:53:43 EVENT [SERVER_STARTED] lskdfjs
# 2010/07/19 12:53:43 ERROR lsdkfjls

begin
  while line = readline()
    if line =~ /2[0-9]{3}\/[0-9]{2}\/[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} (.*)/
      puts $1
    else
      puts line
    end
  end
rescue
  # probably eof
end
