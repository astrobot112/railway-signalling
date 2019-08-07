3a. place the runvhdl.sh(used to compile vhdl and load bit file on board) in the directory named 20140524
    and other files in appropriate places and run runvhdl.sh file
3b. place the comm.sh(used to compile main.c) in the directory named 20140524 and run it
3c. We are only doing the mandatory part of uart communication and for sending and receiving data we are using gtkterm
    open terminal in the folder named 'xr_usb_serial_common_lnx-3.6-and-newer-pak'
    run following commands to start uart communication
    # make
    # sudo insmod ./xr_usb_serial_common.ko
    sudo gtkterm -p /dev/ttyXRUSB0 -s 115200 

