i = 0
while true
    puts "HelloWorld!"
    puts i
    i = i + 1
    if Blink.req_reload?
        break
    end
    sleep 1
end
