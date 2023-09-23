
import binascii
if __name__ == "__main__":
    # read textfile into string 
    with open('aws-root-ca.pem', 'r') as txtfile:
        mytextstring = txtfile.read()

    # change text into a binary array
    binarray = ' '.join(format(ch, 'b') for ch in binascii.hexlify( bytearray(mytextstring)) )

    # save the file
    with open('aws-root-ca.bin', 'w+') as binfile:
        binfile.write(binarray)