----------------------------------------------------------------------------------
-- Company: 
-- Engineer: 
-- 
-- Create Date:    16:41:40 01/23/2018 
-- Design Name: 
-- Module Name:    lab3dec - Behavioral 
-- Project Name: 
-- Target Devices: 
-- Tool versions: 
-- Description: 
--
-- Dependencies: 
--
-- Revision: 
-- Revision 0.01 - File Created
-- Additional Comments: 
--
----------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.std_logic_unsigned.all;


-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx primitives in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity decrypter is
    Port ( clock : in  STD_LOGIC;
           K : in  STD_LOGIC_VECTOR (31 downto 0);
           C : in  STD_LOGIC_VECTOR (31 downto 0);
           P : out  STD_LOGIC_VECTOR (31 downto 0);
			  reset : in  STD_LOGIC;
           done : out STD_LOGIC;
			  enable : in  STD_LOGIC
			   );
end decrypter;

architecture Behavioral of decrypter is

  signal N0 : integer := 0; --N0 is the number of 0s in K
  signal hindex : integer := 31; --hindex is the highest index of K
  signal T : std_logic_vector(3 downto 0);
  signal helpsignal1: std_logic:='0';     -- help signal are used for ordering the loops
  signal helpsignal2: std_logic:='1';
  signal D: std_logic_vector(31 downto 0); --D is used for storing P temporarily
begin

process(clock, reset, enable)
begin
		if (reset = '1') then
			P <= "00000000000000000000000000000000";
			helpsignal1 <= '0';   --When is reset is 1 the signals are assigned
			helpsignal2 <= '1';    --their starting values
			hindex <= 31;
			N0 <= 0;
			done <= '0';
		elsif (clock'event and clock = '1' and enable = '1' ) then
		   if (helpsignal1 ='0' and helpsignal2 = '1') then
			      D <= C;
               T(0) <= K(0) xor K(4) xor K(8) xor K(12) xor K(16) xor K(20) xor K(24) xor K(28); 
			      T(1) <= K(1) xor K(5) xor K(9) xor K(13) xor K(17) xor K(21) xor K(25) xor K(29);
			      T(2) <= K(2) xor K(6) xor K(10) xor K(14) xor K(18) xor K(22) xor K(26) xor K(30);
			      T(3) <= K(3) xor K(7) xor K(11) xor K(15) xor K(19) xor K(23) xor K(27) xor K(31);
		         helpsignal1 <= '1';
			 elsif (helpsignal1 = '1' and helpsignal2 = '1') then --loop for counting number of 0s in K
		          if(hindex = -1) then           --if N0 is calculated next loop will execute
		                helpsignal2 <= '0';      --in next clk edge
		          elsif(hindex /= -1) then
		                if(K(hindex) = '0') then
		                       N0 <= N0 + 1;     --increment N0 if K(hindex) is 0
		                end if;
				          hindex <= hindex - 1;
		          end if;			
			
		    elsif (helpsignal1 = '1' and helpsignal2 = '0') then  -- for adding 15 for first time
		           T <= T + 15;
			        helpsignal1 <= '0';
		    elsif (helpsignal1 = '0'  and helpsignal2 = '0') then --for decrypting text
		           if (N0 = 0) then
			              P <= D;      --P is assigned as D after N0 loops
			              done <= '1';
					  elsif (N0 /= 0) then
			              D <= D xor ( T & T & T & T & T & T & T & T) ;
			              T <= T + 15;
			              N0 <= N0 - 1;   --N1 is decremented after each loop
			        end if;
			 end if;	
		end if;
end process;


end Behavioral;

