while true
    puts "HelloWorld!"
    if Blink.req_reload?
        break
    end
end
