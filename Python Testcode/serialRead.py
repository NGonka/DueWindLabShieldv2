'''
Python 3.4 code !!!
'''
import serial,json,time,csv,sys     #import external modules
                                    #the module pySerial must be installed separately

### setup up ###
ser = serial.Serial('COM37',250000) #Port definiton, adapt to your needs, the baudrate is fixed
message=''
record_minutes=0.3333333 #Defines the record length in minutes
time.sleep(5.0)          #sleeps 5seconds, otherwise the script is to fast 
                         #(the arduino needs some time after the reset)
timestamp_last=0
timestamp=0
diff_last=0
timestamp_start=0;
##Send commands to the Arduino Due, after every write command should be a wait command of at least 0.5 seconds
time.sleep(2.0)
ser.write(b'AA')            #Starts Datastream
time.sleep(0.5)
ser.write(b'ii1000')        #Sets the load to 100.0% (short circuit)

### functions ###
def readlineCR(port):   #routine for checking if start and end brackets 
                        #as well as the 'EOL' (End Of Line) is in t the data packet
                        #reads the data in between
    rv = ""
    start=False

    while True:
        ch = port.read()
        try:
            if ch.decode('utf-8')=='{':
                start=True
        except:
            pass
        if start:
            try:
                rv+=ch.decode('utf-8')
            except:
                print('Fail to decode')
                pass
            try:
                if rv.find('}\r\nEOL',0)!=-1:
                    start=False
                    return rv[:-3]
            except:
                print('Fail to find EOL')
                pass
#end def readlineCR

def validate_json(text):
    #verifies if every JSON object value name is in the datapacket, otherwise it is a fail
    try:
        if text.__contains__('timestamp') & text.__contains__('voltage') &text.__contains__('current')&text.__contains__('windSpeed') &text.__contains__('pitchAngle') &text.__contains__('rpm') &text.__contains__('temperature') &text.__contains__('ambientPressure') &text.__contains__('humidity') &text.__contains__('accelerationX') &text.__contains__('accelerationY') &text.__contains__('accelerationZ') &text.__contains__('currentSink'):
            return True
    except:
        return False
#end def validate_json

### Main Programm ###
with open ('data/Data.csv','w', newline='') as csvfile:
    print(time.strftime('%X %x'))   #prints the time to the console
    fieldnames=['timestamp','voltage','current','windSpeed','pitchAngle','rpm','temperature','ambientPressure','humidity','accelerationX','accelerationY','accelerationZ','currentSink'] #defines the header of the csv file
    writer = csv.DictWriter(csvfile, fieldnames=fieldnames,dialect='excel-tab') #opens a csv-file object,sets the format to excel tab separated
    writer.writeheader() #writes the header to the dataset
    while ser.isOpen():
        message = readlineCR(ser)
        if message.count('{')==1 & message.count('}')==1 :
            try:
                text = json.loads(message)
                if validate_json(text): #verifies the JSON object
                    if timestamp==0:    #checks if there is a starting timestamp
                        timestamp=int(text['timestamp'])
                        timestamp_start=timestamp
                    else:
                        timestamp=int(text['timestamp'])
                    if timestamp>0:
                        writer.writerow(text)   #writes the data to the csv-object
                else:
                    message=''
                    ser.flushOutput()
            except ValueError as err:   #in case of an error, prints it
                print('JSON Error')
                print (err)
                print('Faulty Message: {0}'.format(message))
                message=''
                ser.flushOutput()
                pass
        else:
            message=''
            ser.flushOutput()
        if (timestamp-timestamp_start)>record_minutes*60*1000000: #calculates whether the recording length has been reached
            print(time.strftime('%X %x'))   #prints the time to the console
            ser.write(b'A0')                #stops data stream
            time.sleep(1.0)
            ser.write(b'ii0')               #sets the load back to 0.0% (open circuit)
            sys.exit()                      #closes the command line window if the script is called by double click