#!/usr/bin/env python
#-*- coding: ascii -*-

from __future__ import print_function

def Mul8Bit(a, b):
	t = a * b + 128;
	return (t + (t >> 8)) >> 8;
	
def ExtendNTo8Bits(input, n):
	return (input >> (n - (8 - n))) | (input << (8 - n))

def Extend4To8Bits(input):
	return ExtendNTo8Bits(input, 4)

def Extend5To8Bits(input):
	return ExtendNTo8Bits(input, 5)

def Extend6To8Bits(input):
	return ExtendNTo8Bits(input, 6)

def Extend7To8Bits(input):
	return ExtendNTo8Bits(input, 7)

def GenExpand5Table():
	expand5 = [ 0 for i in range(32) ]
	for i in range(len(expand5)):
		expand5[i] = Extend5To8Bits(i)
	return expand5

def GenExpand6Table():
	expand6 = [ 0 for i in range(64) ]
	for i in range(len(expand6)):
		expand6[i] = Extend6To8Bits(i)
	return expand6

def GenExpand7Table():
	expand7 = [ 0 for i in range(128) ]
	for i in range(len(expand7)):
		expand7[i] = Extend7To8Bits(i)
	return expand7

def ETCGetModifier(cw, selector):
	etc1_modifier_table = (
		(2, 8),
		(5, 17),
		(9, 29),
		(13, 42),
		(18, 60),
		(24, 80),
		(33, 106),
		(47, 183),
	)
	if selector >= 2:
		return etc1_modifier_table[cw][selector - 2]
	else:
		return -etc1_modifier_table[cw][not selector]

def ETC1DecodeValue(diff, inten, selector, packed_c):
	if diff:
		c = Extend5To8Bits(packed_c)
	else:
		c = Extend4To8Bits(packed_c)
	c += ETCGetModifier(inten, selector)
	c = max(0, min(c, 255))
	return c

def PrepareOptTable(expand):
	size = len(expand)
	o_match = [ [ 0 for i in range(2) ] for j in range(256) ]
	for i in range(256):
		best_err = 256
		for min in range(size):
			for max in range(size):
				min_e = expand[min]
				max_e = expand[max]
				err = abs(max_e + Mul8Bit(min_e - max_e, 0x55) - i);
				if err < best_err:
					o_match[i][0] = max
					o_match[i][1] = min
					best_err = err
	return o_match

def PrepareOptTable2(expand):
	size = len(expand)
	o_match = [ [ 0 for i in range(2) ] for j in range(256) ]
	for i in range(256):
		best_err = 256
		for min in range(size):
			for max in range(size):
				combo = (43 * expand[min] + 21 * expand[max] + 32) >> 6;
				err = abs(i - combo);
				if err < best_err:
					o_match[i][0] = min
					o_match[i][1] = max
					best_err = err
	return o_match

def PrepareETC1InverseLookup():
	etc1_inverse_lookup = [ [ 0 for i in range(256) ] for j in range(64) ]
	for diff in range(2):
		if diff != 0:
			limit = 32
		else:
			limit = 16
		for inten in range(8):
			for selector in range(4):
				inverse_table_index = diff + (inten << 1) + (selector << 4)
				for color in range(256):
					best_err = 0xFFFFFFFF
					best_packed_c = 0
					for packed_c in range(limit):
						v = ETC1DecodeValue(diff, inten, selector, packed_c);
						err = abs(v - color);
						if err < best_err:
							best_err = err
							best_packed_c = packed_c
							if best_err == 0:
								break;
					etc1_inverse_lookup[inverse_table_index][color] = best_packed_c | (best_err << 8)
	return etc1_inverse_lookup

if __name__ == "__main__":
	expand5 = GenExpand5Table()
	expand6 = GenExpand6Table()
	expand7 = GenExpand7Table()

	quant_5_tab = [ 0 for i in range(256 + 16) ]
	quant_6_tab = [ 0 for i in range(256 + 16) ]
	for i in range(len(quant_5_tab)):
		v = max(0, min(i - 8, 255))
		quant_5_tab[i] = expand5[Mul8Bit(v, len(expand5) - 1)]
		quant_6_tab[i] = expand6[Mul8Bit(v, len(expand6) - 1)]

	o_match5 = PrepareOptTable(expand5);
	o_match6 = PrepareOptTable(expand6);
	o_match7 = PrepareOptTable2(expand7);

	etc1_inverse_lookup = PrepareETC1InverseLookup();

	source = "// DON'T EDIT THIS FILE. Automatically generated by TableGen.py\n\n"
	source += "namespace KlayGE\n"
	source += "{\n"
	source += "\tnamespace TexCompressionLUT\n"
	source += "\t{\n"
	source += "\t\tstatic uint8_t constexpr EXPAND5[] =\n"
	source += "\t\t{\n"
	source += "\t\t\t"
	for i in range(len(expand5)):
		source += "%d" % expand5[i]
		if i != len(expand5) - 1:
			source += ","
		source += " "
	source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint8_t constexpr EXPAND6[] =\n"
	source += "\t\t{\n"
	source += "\t\t\t"
	for i in range(len(expand6)):
		source += "%d" % expand6[i]
		if i != len(expand6) - 1:
			source += ","
		source += " "
	source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint8_t constexpr EXPAND7[] =\n"
	source += "\t\t{\n"
	source += "\t\t\t"
	for i in range(len(expand7)):
		source += "%d" % expand7[i]
		if i != len(expand7) - 1:
			source += ","
		source += " "
	source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint8_t constexpr O_MATCH5[][2] =\n"
	source += "\t\t{\n"
	for i in range(len(o_match5)):
		source += "\t\t\t{ %d, %d }" % (o_match5[i][0], o_match5[i][1])
		if i != len(o_match5) - 1:
			source += ","
		source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint8_t constexpr O_MATCH6[][2] =\n"
	source += "\t\t{\n"
	for i in range(len(o_match6)):
		source += "\t\t\t{ %d, %d }" % (o_match6[i][0], o_match6[i][1])
		if i != len(o_match6) - 1:
			source += ","
		source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint8_t constexpr O_MATCH7[][2] =\n"
	source += "\t\t{\n"
	for i in range(len(o_match7)):
		source += "\t\t\t{ %d, %d }" % (o_match7[i][0], o_match7[i][1])
		if i != len(o_match7) - 1:
			source += ","
		source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint8_t constexpr QUANT_5_TAB[] =\n"
	source += "\t\t{\n"
	source += "\t\t\t"
	for i in range(len(quant_5_tab)):
		source += "%d" % quant_5_tab[i]
		if i != len(quant_5_tab) - 1:
			source += ","
		source += " "
	source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint8_t constexpr QUANT_6_TAB[] =\n"
	source += "\t\t{\n"
	source += "\t\t\t"
	for i in range(len(quant_6_tab)):
		source += "%d" % quant_6_tab[i]
		if i != len(quant_6_tab) - 1:
			source += ","
		source += " "
	source += "\n"
	source += "\t\t};\n"
	source += "\n"
	source += "\t\tstatic uint16_t constexpr ETC1_INVERSE_LOOKUP[][256] =\n"
	source += "\t\t{\n"
	for i in range(len(etc1_inverse_lookup)):
		source += "\t\t\t{ "
		for j in range(len(etc1_inverse_lookup[i])):
			source += "%d" % etc1_inverse_lookup[i][j]
			if j != len(etc1_inverse_lookup[i]) - 1:
				source += ","
			source += " "
		source += "}"
		if i != len(etc1_inverse_lookup) - 1:
			source += ","
		source += "\n"
	source += "\t\t};\n"
	source += "\t}\n"
	source += "}\n"

	source_file = open("Tables.hpp", "w")
	source_file.write(source)
	source_file.close()
