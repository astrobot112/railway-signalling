
--
-- Copyright (C) 2009-2012 Chris McClelland
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Lesser General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.
--
library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use IEEE.std_logic_unsigned.all;

entity mymodule is
	port(
		clk_in       : in    std_logic;
		reset_in     : in    std_logic;
					-- DVR interface -> Connects to comm_fpga module
		chanAddr_in  : in std_logic_vector(6 downto 0);
		h2fData_in   : in std_logic_vector(7 downto 0);
		h2fValid_in  : in std_logic; 
		h2fReady_out : out std_logic;
		f2hData_out  : out std_logic_vector(7 downto 0); 
		f2hValid_out : out   std_logic;
		f2hReady_in  : in   std_logic;
			
			-- External interface
		sseg_out     : out   std_logic_vector(7 downto 0);
		anode_out      : out   std_logic_vector(3 downto 0);
		led_out      : out   std_logic_vector(7 downto 0);
		sw_in        : in   std_logic_vector(7 downto 0);
		resetswitch    : in    std_logic;
		upswitch       : in    std_logic;
		downswitch     : in    std_logic;
		leftswitch     : in    std_logic;
		rightswitch    : in    std_logic;
		tx : out std_logic;
		rx : in std_logic);

end entity;
architecture structural of mymodule is
           component decrypter 
    port ( clock : in  STD_LOGIC;
           K : in  STD_LOGIC_VECTOR (31 downto 0);
           C : in  STD_LOGIC_VECTOR (31 downto 0);
           P : out  STD_LOGIC_VECTOR (31 downto 0);
			  reset : in  STD_LOGIC;
           done : out STD_LOGIC;
			  enable : in  STD_LOGIC
			   );
end component;
component encrypter 
    port ( clock : in  STD_LOGIC;
           K : in  STD_LOGIC_VECTOR (31 downto 0);
           P : in  STD_LOGIC_VECTOR (31 downto 0);
			  C : out  STD_LOGIC_VECTOR (31 downto 0);
           reset : in  STD_LOGIC;
			  done : out STD_LOGIC;
           enable : in  STD_LOGIC);
end component;

component baudrate_gen is
	port (clk	: in std_logic;
			rst	: in std_logic;
			sample: out std_logic);
end component baudrate_gen;
	
component uart_rx is			
	port (clk	: in std_logic;
			rst	: in std_logic;
			rx		: in std_logic;
			sample: in STD_LOGIC;
			rxdone: out std_logic;
			rxdata: out std_logic_vector(7 downto 0));
end component uart_rx;
	
component uart_tx is			
	port (clk    : in std_logic;
			rst    : in std_logic;
			txstart: in std_logic;
			sample : in std_logic;
			txdata : in std_logic_vector(7 downto 0);
			txdone : out std_logic;
			tx	    : out std_logic);
end component uart_tx;
   	  	

	-- Flags for display on the 7-seg decimal points
	signal flags                  : std_logic_vector(3 downto 0);
	--31 bit registers used for incryption and decryption
	
	--two acks which are agreed by host and board
        signal ack1 : std_logic_vector(31 downto 0):= "11001010110010101100101011001010";
	signal ack2 : std_logic_vector(31 downto 0):= "11111010110010101100101011001010";
    	--signals used to send coordinates to host
    	signal az : std_logic_vector(23 downto 0) := (others => '0');
	signal coor :std_logic_vector(7 downto 0) := "01001000";

	-- Registers implementing the channels
	signal checksum, checksum_next : std_logic_vector(15 downto 0) := (others => '0');
	
	--signals used in encryption and decryption
	signal sw_in1, sw_in2 , a11, rc, rcc, ra, raa, r11, r12, r21, r22, 
		fia, fi,f11,fia4,f22,a22,makesw,encsw : std_logic_vector(31 downto 0);
	signal reset1,reset4,reset2,reset7 : std_logic :='1';
	signal done1,done2,done3,done4,done5,done6,done7,done8 : std_logic := '0';
	signal enable1,enable2,enable3,enable4,enable5,enable6,enable7,enable8 : std_logic;

	--signals used for uart communication
	signal resettx,rcount,resetrx,txstart,sample,sample1,rxdone,txdone : std_logic := '0';
	signal rxdata : std_logic_vector(7 downto 0); 

	--signals used for controlling flow of loops and time	
	signal count,countx ,count1,count2,count3,counti,time1,time2,temprxdone: integer := 0; 
	--count is used to give the 8 inputs and after 8 inputs are given it is used to count the time
	--gap between two led outputs--count1 is used to start giving the modified outputs to led 
	--after 8 inputs are received	

	--registers used in transfer of data between host and board						 
	signal  s1, s2, s3, s4, a1, a2,a3,a4,fia0,fia1,fia2,fia3,switch1,reg0,r0,r1,r2,r3,r4,r5,r6,
		r7,reg0_next,rc0,rc1,rc2,rc3,ra4,ra5,ra6,ra7,outp4_1,outp4_2,outp4_3,outp5_1,outp5_2,
		outp5_3,outp6_1,outp6_2,outp6_3,outp7_1,outp7_2,outp7_3,out4_1,out4_2,out4_3,out5_1,
		out5_2,out5_3,out6_1,out6_2,out6_3,out7_1,out7_2,out7_3,m1,m2,m3,m4,m5,m6,m7,m8,outp0,
		outp1,outp2,outp3,outp4,outp5,outp6,outp7,out0,out1,out2,out3,out4,out5,out6,out7,rcc1,
		rcc2,rcc3,rcc4: std_logic_vector(7 downto 0)  := (others => '0');

	signal ack8bit: std_logic_vector(7 downto 0)  := "11111111";
	
					    --r0 to r7 are used to store the 8 inputs 
					    --out0 to out7 are used to modify the input according to constraints given and output them to l


begin                                                                     --BEGIN_SNIPPET(registers)

	-- Infer registers
	process(clk_in,count1)
	begin
		if ( rising_edge(clk_in) ) then
			if ( reset_in = '1') then
				reg0 <= (others => '0');
				checksum <= (others => '0');
				count <= 0;
				count1 <= 0;counti<=0;reset1<='1';reset4<='1';count2<=0;count3<=0;
				time1<=0;time2<=0;reset2<='1';reset7<='1';
				
				
			else
				
					
   					reg0 <= reg0_next;
					checksum <= checksum_next;
				if(resetswitch = '1') then --S1
					count1<=20;
					count<=0;
					led_out<="11111111";
				elsif(count1=20) then
					count<=count+1;
					if(count=300000000) then
						reg0 <= (others => '0');
						checksum <= (others => '0');
						count <= 0;
				                count1 <=0;counti<=0;reset1<='1';reset4<='1';count2<=0;count3<=0;time1<=0;
						time2<=0;reset2<='1';reset7<='1';
						led_out<="00000000";
					end if;
				elsif(count1=0) then --S2 starts here
					reset1 <= '0'; enable1 <='1';count1<=1; 
				elsif(count1=1 and done1='1') then --coordinates are encrypted and sent to the host here
					if(count=0 and counti=0) then
					 count<=1;
					elsif(count=1 and counti=0) then
					 count<=2;
					elsif(count=2 and counti=0) then
					 count<=3;
					elsif(count=3 and counti=0 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=1;enable1<='0'; 
					elsif(counti=1 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=2;
					elsif(counti=2 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=3;
					elsif(counti=3 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=4;count1<=2;count<=0;end if;
				elsif(count1=2 ) then --coordinates are received from host
					time1<=time1+1;
					if(count=0 and h2fValid_in = '1' and chanAddr_in = "00000011") then 
					 count<=1;reset2<='0';
					elsif(count=1 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						rc0<=reg0;count<=count+1;   
					elsif(count=2 and h2fValid_in = '1'  and chanAddr_in = "00000011")then
						rc1<=reg0;count<=count+1;
					elsif(count=3 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						rc2<=reg0;count<=count+1;
					elsif(count=4)then
						rc3<=reg0;count<=5;
                                                enable3<='1';enable2<='1';
					elsif(sw_in1=rcc and count=5 and done3='1') then
						count1<=3;count<=0;
					elsif(sw_in1/=rcc and done3='1'and count=5) then
						count<=0;reset2<='1';
					elsif(time1>1900000000 and time2/=13) then
						time1<=0;time2<=time2+1;
					elsif(time1>1900000000 and time2=13) then
						time1<=0;time2<=time2+1;
					elsif(time1>900000000 and time2=14) then
						count1<=0;time2<=0;time1<=0;
					
					end if;
				elsif(count1=3 and done3='1' and done2='1') then --ack1 is sent to host
		
					if(count=0 and counti=4) then 
					 count<=4;counti<=5;enable3<='0';enable2<='0';
					elsif(count=4 and counti=5 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=6;
					elsif(counti=6 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=7;
					elsif(counti=7 and f2hReady_in='1'and chanAddr_in = "0000010") then
						counti<=8;
					elsif(counti=8 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=0;count1<=4;count<=0;end if;
				elsif(count1=4) then --ack2 is received from host
					time1<=time1+1;
					if(count=0 and h2fValid_in = '1' and chanAddr_in = "00000011") then
						count<=1;reset4<='0';
					elsif(count=1 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						ra4<=reg0;count<=count+1; 
					elsif(count=2 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						ra5<=reg0;count<=count+1;
					elsif(count=3 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						ra6<=reg0;count<=count+1;
					elsif(count=4)then  
						ra7<=reg0;count<=count+1;
					elsif(count=5)then
						enable4<='1';count<=count+1;led_out<="00110111";
					elsif(raa=ack2 and count=6 and done4='1') then
						count1<=5; enable4<='0';count<=0;led_out<="01110111";
					elsif(raa/=ack2 and done4='1'and count=6) then
						count<=0;reset4<='1';led_out<="11110111";
					elsif(time1>1900000000 and time2/=13) then
						time1<=0;time2<=time2+1;
					elsif(time1>1900000000 and time2=13) then
						time1<=0;time2<=time2+1;
					elsif(time1>900000000 and time2=14) then
						count1<=0;time2<=0;time1<=0;
					end if;


				elsif(count1=5 and done4='1')then --first four bytes of data is received 
					if(count=0 and h2fValid_in = '1' and chanAddr_in = "00000011") then 
						count<=count+1;
					elsif(count=1 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						r0<=reg0;count<=count+1;    --stores the input into register
					elsif(count=2 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						r1<=reg0;count<=count+1;
					elsif(count=3 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						r2<=reg0;count<=count+1;
					elsif(count=4)then 
						r3<=reg0;count<=0;count1<=6;enable5<='1';end if;
                                                           
				
				elsif(count1=6 and done5='1') then --ack1 is sent 
					if(count=0 and counti=0) then
					 count<=1;
					elsif(count=1 and counti=0) then
					 count<=2;
					elsif(count=2 and counti=0) then
					 count<=3;counti<=5;
					elsif(counti=5 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=counti+1;enable5<='0';
					elsif(counti=6 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=counti+1;
					elsif(counti=7 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=counti+1;
					elsif(counti=8 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=0;count1<=7;count<=0;end if;	
				
				
				elsif(count1=7)then --next four bytes of data is received
					if(count=0 and h2fValid_in = '1' and chanAddr_in = "00000011") then
						count<=1;
					elsif(count=1 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						r4<=reg0;count<=count+1;       
					elsif(count=2 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						r5<=reg0;count<=count+1;
					elsif(count=3 and h2fValid_in = '1'  and chanAddr_in = "00000011")then
						r6<=reg0;count<=count+1;
					elsif(count=4)then
						r7<=reg0;count<=0;count1<=8;
                    				enable6<='1';	end if;

				
				elsif(count1=8 and done6='1') then --ack1 is sent
					if(count=0 and counti=0) then
					 count<=1;enable6<='0';
					elsif(count=1 and counti=0) then
					 count<=2;
					elsif(count=2 and counti=0) then
					 count<=3;counti<=5;
					elsif(counti=5 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=counti+1;enable5<='0';
					elsif(counti=6 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=counti+1;
					elsif(counti=7 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=counti+1;
					elsif(counti=8 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=12;count1<=9;count<=0;
					end if;
				elsif(count1=9)then --final ack2 is received
					
					if(count=0 and h2fValid_in = '1' and chanAddr_in = "00000011") then 
						count<=1;reset7<='0';
					elsif(count=1 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						fia0<=reg0;count<=count+1;       
					elsif(count=2 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						fia1<=reg0;count<=count+1;
					elsif(count=3 and h2fValid_in = '1' and chanAddr_in = "00000011")then
						fia2<=reg0;count<=count+1;
					elsif(count=4)then
						fia3<=reg0;count<=5;enable7<='1';
					elsif(f22=ack2 and count=5 and done7='1') then
						count1<=10;count<=0;enable7<='0';
					elsif(f22/=ack2 and done7='1'and count=5) then
						count<=0;reset7<='1';
					elsif(time1>1900000000 and time2/=13) then
						time1<=0;time2<=time2+1;
					elsif(time1>1900000000 and time2=13) then
						time1<=0;time2<=time2+1;
					elsif(time1>900000000 and time2=14) then
						count1<=0;time2<=0;time1<=0;
					end if;
				elsif(count1=10) then --input from slider switches is taken for railways coming in 8 directions  
					switch1<=sw_in;
					count1<=11;led_out<="10100000";
					enable7<='0';
				elsif(count1=11)then --signals in 8 directions are displayed on led outputs
					count<=count+1;
							
					if(count=100000000 and count2=0) then 		
						led_out<=out0;	
						
					elsif(count=400000000 and count2=0) then
						led_out<=out1;
					
					elsif(count=700000000 and count2=0) then
						led_out<=out2;

					elsif(count=1000000000 and count2=0) then
						led_out<=out3;
					elsif(count=1300000000 and count2=0) then
						led_out<=out4_3;
					elsif(count=1400000000 and count2=0) then
						led_out<=out4_2;
					elsif(count=1500000000 and count2=0) then
						led_out<=out4_1;
					elsif(count=1600000000 and count2=0) then
						led_out<=out5_1;
					elsif(count=1700000000 and count2=0) then
						led_out<=out5_2;
					elsif(count=1800000000 and count2=0) then
						led_out<=out5_3;
					elsif(count=1900000000 and count2=0) then
						led_out<=out6_1;
						count2<=1;count<=0;
					elsif(count=100000000 and count2=1) then
						led_out<=out6_2;
					elsif(count=200000000 and count2=1) then
						led_out<=out6_3;
					elsif(count=300000000 and count2=1) then
						led_out<=out7_1;
					elsif(count=400000000 and count2=1) then
						led_out<=out7_2;
					elsif(count=500000000 and count2=1) then
						led_out<=out7_3;
				        elsif(count=600000000 and count2=1) then
						led_out<="00000000";
						count2<=0;
						count<=0;count1<=count1+1;
					end if;
				elsif(count1=12) then --S3 starts here checks if upswitch is pressed
					count<=count+1;
					if(upswitch='1') then
						count1<=count1+1;count<=0;
					elsif(count>1900000000) then
						count1<=16;count<=0;end if;
					
				elsif(downswitch='1' and count1=13) then --checks for downsitch after upswitch
					count1<=count1+1;
				elsif(count1=14) then -- 8 bit data from slider switches is received using fpgalink and is encrypted
					if(count=0) then
					count<=1;enable8<='1';
					elsif(count=1  and done8='1') then
					 count<=2;enable8<='0';
					elsif(count=2) then
					 count<=3;
					elsif(count=3) then
					 count<=4;count1<=count1+1;end if;
				elsif(count1=15) then -- the 8 bit data from fpga link is sent to host
					if(count=4) then 
						counti<=13;count<=1;led_out<="11111111";
					elsif(counti=13 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=17;led_out<="11111110";					
					elsif(counti=17 and f2hReady_in='1' and chanAddr_in = "0000010") then
						enable8<='0';counti<=counti+1;led_out<="10101010";
					elsif(counti=18 and f2hReady_in='1'and chanAddr_in = "0000010") then
						counti<=counti+1;led_out<="11111101";
					elsif(counti=19 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=counti+1;led_out<="11111011";
					elsif(counti=20 and f2hReady_in='1' and chanAddr_in = "0000010") then
						counti<=21;led_out<="10100000";count1<=16;count<=0;
					end if;
			
				elsif(rcount='0' and rxdone='1' and rxdata="11111111") then
					rcount<='1';led_out<="10101010";
				elsif(rcount='1' and rxdone='1') then --checks for data from host in uart communication
					rcount<='0'; temprxdone<=1;
				elsif(count1=16) then --S4 starts here checks for leftswitch and timeout after 38 secs
					count<=count+1;
					if(leftswitch='1') then
						count1<=count1+1;count<=0;resettx<='1';
					elsif(count>1900000000 and countx=0) then
						count<=0;countx<=1;
					elsif(count>1900000000 and countx=1) then
						count1<=19;count<=0;countx<=0;end if;
				elsif(downswitch='1' and count1=17) then --checks for downswitch
					count1<=count1+1;resettx<='0';txstart<='1';
				elsif(count1=18 and txdone='1') then --data is sent to host
					txstart<='0';count1<=count1+1;
				elsif(count1=19) then --S5 starts here if data is received from host in uart communication 
					count<=count+1; 
					if(temprxdone=1) then
						count1<=count1+1;count<=0;temprxdone<=2;
					elsif(count>1900000000 and countx=0) then
						count<=0;countx<=1;
					elsif(count>1900000000 and countx=1) then
						count1<=21;count<=0;countx<=0;
					end if;
				elsif(count1=21) then --waits in S6 for 10 secs then goes to S2
					count<=count+1;
					if(count>1000000000) then
						count1<=0;count<=0;counti<=0;temprxdone<=0;end if;
				end if;



		
					
					
				
			end if;
		end if;
	end process;

	-- Drive register inputs for each channel when the host is writing
           
	reg0_next <=
		h2fData_in when chanAddr_in = "00000011" and h2fValid_in = '1' 
		else reg0;
	
	checksum_next <=
		std_logic_vector(unsigned(checksum) + unsigned(h2fData_in))
			when chanAddr_in = "0000000" and h2fValid_in = '1'
		else h2fData_in & checksum(7 downto 0)
			when chanAddr_in = "0000001" and h2fValid_in = '1'
		else checksum(15 downto 8) & h2fData_in
			when chanAddr_in = "0000010" and h2fValid_in = '1'
		else checksum;
	-- Select values to return for each channel when the host is reading
           sw_in1 <= az & coor;
	   makesw <= az & sw_in;
	   rc <= rc0 & rc1 & rc2 & rc3;
	   ra <= ra4 & ra5 & ra6 & ra7;
	   fia <= fia0 & fia1 & fia2 & fia3;
    	    	U1 : encrypter port map(clock => clk_in,
                             	K => "10101010111110010101101011101010",
                             	P => sw_in1,
                             	C => sw_in2,
                             	reset => reset1,
                                    done => done1,
                             	enable => enable1);
             	U2 : encrypter port map(clock => clk_in,
                             	K => "10101010111110010101101011101010",
                             	P => ack1,
                             	C => a11,
                             	reset => reset1,
                                done => done2,
                             	enable => enable2);
	    	U3 : decrypter port map(clock => clk_in,
                	       	     K => "10101010111110010101101011101010",
                             	     C => rc,
                             	     P => rcc,
                             	     reset => reset2,
                                     done => done3,
                             	     enable => enable3);
	   			
	    	U4 : decrypter port map (clock => clk_in,
                                     K => "10101010111110010101101011101010",
                                     C => ra,
                                     P => raa,
                                     reset => reset4,
                                     done => done4,
                                     enable => enable4);
	    	r11 <= r0 & r1 & r2 & r3;
           	U5 : decrypter port map (clock => clk_in,
                                     K => "10101010111110010101101011101010",
                                     C => r11,
                                     P => r12,
                                     reset => reset1,
                                     done => done5,
                             	     enable => enable5);
	   			r21 <= r4 & r5 & r6 & r7;
	   	U6 : decrypter port map (clock => clk_in,
                                    K => "10101010111110010101101011101010",
                             	    C => r21,
                             	    P => r22,
                             	    reset => reset1,
                                    done => done6,
                             	    enable => enable6);
	  			
          	U7 : decrypter port map (clock => clk_in,
                        	    K => "10101010111110010101101011101010",
                             	    C => fia,
                             	    P => f22,
                             	    reset => reset7,
                                    done => done7,
                             	    enable => enable7);
		U8 : encrypter port map(clock => clk_in,
                             	K => "10101010111110010101101011101010",
                             	P => makesw,
                             	C => encsw,
                             	reset => reset1,
                                    done => done8,
                             	enable => enable8);
		m1 <= encsw(31 downto 24);   
		m2<=encsw(23 downto 16) ;
		m3<=encsw(15 downto 8);
		m4<=encsw(7 downto 0);
		m5<=r21(31 downto 24);   
		m6<=r21(23 downto 16) ;
		m7<=r21(15 downto 8);
		m8<=r21(7 downto 0);
	    	s1 <= sw_in2(31 downto 24);
	    	s2 <= sw_in2(23 downto 16);
	    	s3 <= sw_in2(15 downto 8);
	    	s4 <= sw_in2(7 downto 0);
	        a1 <= a11(31 downto 24);
	    	a2 <= a11(23 downto 16);
	    	a3 <= a11(15 downto 8);
	    	a4 <= a11(7 downto 0);
	   	rcc1 <= rcc(31 downto 24);
	    	rcc2 <= rcc(23 downto 16);
	    	rcc3 <= rcc(15 downto 8);
	    	rcc4 <= rcc(7 downto 0);
		f2hData_out <=
			s1             when chanAddr_in = "0000010" and counti = 0
			else s2        when chanAddr_in = "0000010" and counti = 1
			else s3	       when chanAddr_in = "0000010" and counti = 2
			else s4	       when chanAddr_in = "0000010" and counti = 3
			else a1        when chanAddr_in = "0000010" and counti = 5
			else a2        when chanAddr_in = "0000010" and counti = 6
			else a3	       when chanAddr_in = "0000010" and counti = 7
			else a4	       when chanAddr_in = "0000010" and counti = 8
			else ra4       when chanAddr_in = "0000010" and counti = 9
			else ra5       when chanAddr_in = "0000010" and counti = 10
			else ra6       when chanAddr_in = "0000010" and counti = 11
			else ra7       when chanAddr_in = "0000010" and counti = 12
			else ack8bit        when chanAddr_in = "0000010" and counti = 13
			else ack8bit        when chanAddr_in = "0000010" and counti = 14
			else ack8bit       when chanAddr_in = "0000010" and counti = 15
			else ack8bit   when chanAddr_in = "0000010" and counti = 16
			else m1  when chanAddr_in = "0000010" and counti = 17
			else m2    when chanAddr_in = "0000010" and counti = 18
			else m3  when chanAddr_in = "0000010" and counti = 19
			else m4    when chanAddr_in = "0000010" and counti = 20
			else "00000000";
	
		-- Assert that there's always data for reading, and always room for writing
	
	-- LEDs and 7-seg display
	outp0<= "10000" & out0(2 downto 0)
			when rxdata(7)='0' and rxdata(6)='0' and rxdata(5)='0' and rxdata(3)='0'
		else out0;
	outp1<= "10000" & out1(2 downto 0)
			when rxdata(7)='0' and rxdata(6)='0' and rxdata(5)='1' and rxdata(3)='0'
		else out1;
	outp2<= "10000" & out2(2 downto 0)
			when rxdata(7)='0' and rxdata(6)='1' and rxdata(5)='0' and rxdata(3)='0'
		else out2;
	outp3<= "10000" & out3(2 downto 0)
			when rxdata(7)='0' and rxdata(6)='1' and rxdata(5)='1' and rxdata(3)='0'
		else out3;
	outp4_1<= "10000" & out4_1(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='0' and rxdata(5)='0' and rxdata(3)='0'
		else out4_1;
	outp4_2<= "10000" & out4_2(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='0' and rxdata(5)='0' and rxdata(3)='0'
		else out4_2;
	outp4_3<= "10000" & out4_3(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='0' and rxdata(5)='0' and rxdata(3)='0'
		else out4_3;
	outp5_1<= "10000" & out5_1(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='0' and rxdata(5)='1' and rxdata(3)='0'
		else out5_1;
	outp5_2<= "10000" & out5_2(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='0' and rxdata(5)='1' and rxdata(3)='0'
		else out5_2;
	outp5_3<= "10000" & out5_3(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='0' and rxdata(5)='1' and rxdata(3)='0'
		else out5_3;
	outp6_1<= "10000" & out6_1(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='1' and rxdata(5)='0' and rxdata(3)='0'
		else out6_1;
	outp6_2<= "10000" & out6_2(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='1' and rxdata(5)='0' and rxdata(3)='0'
		else out6_2;
	outp6_3<= "10000" & out6_3(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='1' and rxdata(5)='0' and rxdata(3)='0'
		else out6_3;
	outp7_1<= "10000" & out7_1(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='1' and rxdata(5)='1' and rxdata(3)='0'
		else out7_1;
	outp7_2<= "10000" & out4_2(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='1' and rxdata(5)='1' and rxdata(3)='0'
		else out7_2;
	outp7_3<= "10000" & out4_3(2 downto 0)
			when rxdata(7)='1' and rxdata(6)='1' and rxdata(5)='1' and rxdata(3)='0'
		else out7_3;
	out0 <= "10000" & r12(29 downto 27)  --modifies the register values according to constraints given
			when r12(31)='0' or r12(30)='0' or switch1(0) = '0' or (switch1(0) = '1' and switch1(4) = '1') 
		else "01000" & r12(29 downto 27)
			when switch1(0) = '1' and r12(31)='1' and r12(30)='1' and switch1(4)='0' and r12(26)='0' and r12(25)='0' 
				and r12(24)='1' 
		else "00100" & r12(29 downto 27)
			when switch1(0) = '1' and switch1(4) = '0';
	out1 <= "10000" & r12(21 downto 19)
			when r12(23)='0' or r12(22)='0' or switch1(1) = '0' or (switch1(1) = '1' and switch1(5) = '1')
		else "01000" & r12(21 downto 19)
			when switch1(1) = '1' and r12(23)='1' and r12(22)='1' and switch1(5)='0' and r12(18)='0' and r12(17)='0' 
				and r12(16)='1'
		else "00100" & r12(21 downto 19)
			when switch1(1) = '1' and switch1(5) = '0';

			
	out2 <=  "10000" & r12(13 downto 11)
			when r12(15)='0' or r12(14)='0' or switch1(2) = '0' or (switch1(2) = '1' and switch1(6) = '1')
		else "01000" & r12(13 downto 11)
			when switch1(2) = '1' and r12(15)='1' and r12(14)='1' and switch1(6)='0' and r12(10)='0' and r12(9)='0' 
				and r12(8)='1'
		else "00100" & r12(13 downto 11)
			when switch1(2) = '1' and switch1(6) = '0';

			
	out3 <=  "10000" & r12(5 downto 3)
			when r12(7)='0' or r12(6)='0' or switch1(3) = '0' or (switch1(3) = '1' and switch1(7) = '1')
		else "01000" & r12(13 downto 11)
			when switch1(3) = '1' and r12(7)='1' and r12(6)='1' and switch1(7)='0' and r12(2)='0' and r12(1)='0' 
				and r12(0)='1'
		else "00100" & r12(5 downto 3)
			when switch1(3) = '1' and switch1(7) = '0';
	out4_1 <="10000" & r22(29 downto 27)
			when r22(31)='0' or r22(30)='0' or switch1(4) = '0' or (switch1(4) = '1' and switch1(0) = '1')
		else "00100" & r22(29 downto 27)
			when (switch1(4) = '1' and switch1(0) = '0') or (switch1(4) = '1' and switch1(0) = '1')
		else "01000" & r22(29 downto 27)
			when (switch1(4) = '1' and switch1(0) = '1') or (switch1(4) = '1' and r22(31)='1' and r22(30)='1' and 					switch1(0)='0' and r22(26)='0' and r22(25)='0' and r22(24)='1');

	out4_2 <="10000" & r22(29 downto 27)
			when r22(31)='0' or r22(30)='0' or switch1(4) = '0' or (switch1(4) = '1' and switch1(0) = '1')
		else "00100" & r22(29 downto 27)
			when (switch1(4) = '1' and switch1(0) = '0') or (switch1(4) = '1' and switch1(0) = '1')
		else "01000" & r22(29 downto 27)
			when (switch1(4) = '1' and switch1(0) = '1') or (switch1(4) = '1' and r22(31)='1' and r22(30)='1' and 					switch1(0)='0' and r22(26)='0' and r22(25)='0' and r22(24)='1');

	out4_3 <="10000" & r22(29 downto 27)
			when r22(31)='0' or r22(30)='0' or switch1(4) = '0' or (switch1(4) = '1' and switch1(0) = '1')
		else "00100" & r22(29 downto 27)
			when (switch1(4) = '1' and switch1(0) = '0') or (switch1(4) = '1' and switch1(0) = '1')
		else "01000" & r22(29 downto 27)
			when (switch1(4) = '1' and switch1(0) = '1') or (switch1(4) = '1' and r22(31)='1' and r22(30)='1' and 					switch1(0)='0' and r22(26)='0' and r22(25)='0' and r22(24)='1');

----------------------------------------------------		
	out5_1 <= "10000" & r22(21 downto 19)
			when r22(23)='0' or r22(22)='0' or switch1(5) = '0' or (switch1(5) = '1' and switch1(1) = '1')
		else "01000" & r22(21 downto 19)
			when (switch1(5) = '1' and switch1(1) = '1') or (switch1(5) = '1' and r22(23)='1' and r22(22)='1' and 
				switch1(1)='0' and r22(18)='0' and r22(17)='0' and r22(16)='1')
		else "00100" & r22(21 downto 19)
			when (switch1(5) = '1' and switch1(1) = '0') or (switch1(5) = '1' and switch1(1) = '1');
	out5_2 <= "10000" & r22(21 downto 19)
			when r22(23)='0' or r22(22)='0' or switch1(5) = '0' or (switch1(5) = '1' and switch1(1) = '1')
		else "01000" & r22(21 downto 19)
			when (switch1(5) = '1' and switch1(1) = '1') or (switch1(5) = '1' and r22(23)='1' and r22(22)='1' and 
				switch1(1)='0' and r22(18)='0' and r22(17)='0' and r22(16)='1')
		else "00100" & r22(21 downto 19)
			when (switch1(5) = '1' and switch1(1) = '0') or (switch1(5) = '1' and switch1(1) = '1');
	out5_3 <= "10000" & r22(21 downto 19)
			when r22(23)='0' or r22(22)='0' or switch1(5) = '0' or (switch1(5) = '1' and switch1(1) = '1')
		else "01000" & r22(21 downto 19)
			when (switch1(5) = '1' and switch1(1) = '1') or (switch1(5) = '1' and r22(23)='1' and r22(22)='1' and 
				switch1(1)='0' and r22(18)='0' and r22(17)='0' and r22(16)='1')
		else "00100" & r22(21 downto 19)
			when (switch1(5) = '1' and switch1(1) = '0') or (switch1(5) = '1' and switch1(1) = '1');
	out6_1 <= "10000" & r22(13 downto 11)
			when r22(15)='0' or r22(14)='0' or switch1(6) = '0' or (switch1(6) = '1' and switch1(2) = '1')
		else "01000" & r22(13 downto 11)
			when (switch1(6) = '1' and switch1(2) = '1') or (switch1(6) = '1' and r22(15)='1' and r22(14)='1' and 
				switch1(2)='0' and r22(10)='0' and r22(9)='0' and r22(8)='1')
		else "00100" & r22(13 downto 11)
			when (switch1(6) = '1' and switch1(2) = '0') or (switch1(6) = '1' and switch1(2) = '1');
	out6_2 <= "10000" & r22(13 downto 11)
			when r22(15)='0' or r22(14)='0' or switch1(6) = '0' or (switch1(6) = '1' and switch1(2) = '1')
		else "01000" & r22(13 downto 11)
			when (switch1(6) = '1' and switch1(2) = '1') or (switch1(6) = '1' and r22(15)='1' and r22(14)='1' and 
				switch1(2)='0' and r22(10)='0' and r22(9)='0' and r22(8)='1')
		else "00100" & r22(13 downto 11)
			when (switch1(6) = '1' and switch1(2) = '0') or (switch1(6) = '1' and switch1(2) = '1');
	out6_3 <= "10000" & r22(13 downto 11)
			when r22(15)='0' or r22(14)='0' or switch1(6) = '0' or (switch1(6) = '1' and switch1(2) = '1')
		else "01000" & r22(13 downto 11)
			when (switch1(6) = '1' and switch1(2) = '1') or (switch1(6) = '1' and r22(15)='1' and r22(14)='1' and 
				switch1(2)='0' and r22(10)='0' and r22(9)='0' and r22(8)='1')
		else "00100" & r22(13 downto 11)
			when (switch1(6) = '1' and switch1(2) = '0') or (switch1(6) = '1' and switch1(2) = '1');
	out7_1 <= "10000" & r22(5 downto 3)
			when r22(7)='0' or r22(6)='0' or switch1(7) = '0' or (switch1(7) = '1' and switch1(3) = '1')
		else "01000" & r22(5 downto 3)
			when (switch1(7) = '1' and switch1(3) = '1') or (switch1(7) = '1' and r22(7)='1' and r22(6)='1' and 		
				switch1(3)='0' and r22(2)='0' and r22(1)='0' and r22(0)='1')
		else "00100" & r22(5 downto 3)
			when (switch1(7) = '1' and switch1(3) = '0') or (switch1(7) = '1' and switch1(3) = '1');
	out7_2 <= "10000" & r22(5 downto 3)
			when r22(7)='0' or r22(6)='0' or switch1(7) = '0' or (switch1(7) = '1' and switch1(3) = '1')
		else "01000" & r22(5 downto 3)
			when (switch1(7) = '1' and switch1(3) = '1') or (switch1(7) = '1' and r22(7)='1' and r22(6)='1' and 		
				switch1(3)='0' and r22(2)='0' and r22(1)='0' and r22(0)='1')
		else "00100" & r22(5 downto 3)
			when (switch1(7) = '1' and switch1(3) = '0') or (switch1(7) = '1' and switch1(3) = '1');
	out7_3 <= "10000" & r22(5 downto 3)
			when r22(7)='0' or r22(6)='0' or switch1(7) = '0' or (switch1(7) = '1' and switch1(3) = '1')
		else "01000" & r22(5 downto 3)
			when (switch1(7) = '1' and switch1(3) = '1') or (switch1(7) = '1' and r22(7)='1' and r22(6)='1' and 		
				switch1(3)='0' and r22(2)='0' and r22(1)='0' and r22(0)='1')
		else "00100" & r22(5 downto 3)
			when (switch1(7) = '1' and switch1(3) = '0') or (switch1(7) = '1' and switch1(3) = '1');

	f2hValid_out <= '1';
	h2fReady_out <= '1';
   
		
     
	flags <= "00" & f2hReady_in & reset_in;
	seven_seg : entity work.seven_seg
		port map(
			clk_in     => clk_in,
			data_in    => checksum,
			dots_in    => flags,
			segs_out   => sseg_out,
			anodes_out => anode_out
		);
	i_brg : baudrate_gen port map (clk => clk_in, rst => resettx, sample => sample);
	
	
									
	i_tx : uart_tx port map( clk => clk_in, rst => resettx,
                            txstart => txstart,
                            sample => sample, txdata => sw_in,
                            txdone => txdone, tx => tx);
	i_brgrx : baudrate_gen port map (clk => clk_in, rst => resetrx, sample => sample1);	
	
	i_rx : uart_rx port map( clk => clk_in, rst => resetrx,
                            rx => rx, sample => sample1,
                            rxdone => rxdone, rxdata => rxdata);
end architecture;



