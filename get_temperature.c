#include <wiringPi.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <pthread.h>

#include <mysql/mysql.h>

#define MAXTIMINGS 85

#define MAX 5

#define DBHOST "localhost"
#define DBUSER "root"
#define DBPASS "root"
#define DBNAME "demofarmdb"

#define RETRY 5
MYSQL *connector;
MYSQL_RES *result;
MYSQL_ROW row;

static int DHTPIN = 7;
int buffer[MAX];
int fill_ptr = 0;
int use_ptr = 0;
int count = 0;

int ret_humid, ret_temp;


static int dht22_dat[5] = {0,0,0,0,0};

int get_temperature_sensor();

int read_dht22_dat_temp();

static uint8_t sizecvt(const int read)
{
	if (read > 255 || read < 0)
	{
		printf("Invalid data from wiringPi libray\n");
		exit(EXIT_FAILURE);
	}
	return (uint8_t)read;
}



void put(int value) {
	buffer[fill_ptr] = value;
	fill_ptr = (fill_ptr + 1) %MAX;
	count++;
}

int get() {
	int tmp = buffer[use_ptr];
	use_ptr = (use_ptr + 1) % MAX;
	count--;

	connector = mysql_init(NULL);
	int adcChannel = 0;
	int adcValue[0];
	
	if(!mysql_real_connect(connector, DBHOST, DBUSER, DBPASS, DBNAME, 3306, NULL, 0)){
		fprintf(stderr, "%s\n", mysql_error(connector));
		return 0;
	}
	

	char query[1024];
	
	adcValue[0] = get_temperature_sensor();


	sprintf(query, "insert into thi values (now(),%d)", adcValue[0]);

	if(mysql_query(connector, query))
	{			
		fprintf(stderr, "%s\n", mysql_error(connector));
		printf("Write DB error\n");
	}

	mysql_close(connector);

	return tmp;
}

pthread_cond_t empty, fill;
pthread_mutex_t mutex;

void *producer(void *arg) {
	int i;
	for(i = 0; i <10; i++) {
	       pthread_mutex_lock(&mutex);
	       while (count == MAX)
		       pthread_cond_wait(&empty, &mutex);
	       
	       int temper = read_dht22_dat_temp(); 
	       put(temper);
	       pthread_cond_signal(&fill);
	       pthread_mutex_unlock(&mutex);
	}	       
}

void *consumer(void *arg){
	int i;
	for(i = 0; i < 10; i++) {
		pthread_mutex_lock(&mutex);
		while (count == 0)
			pthread_cond_wait(&fill, &mutex);
	
		int tmp = get();

		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&mutex);

	}
}

int main(void)
{
	if(wiringPiSetup() == -1)
	{
		fprintf(stdout, "Unable to start wiringPi: %s\n", strerror(errno));	
		return 1;
	}

	if (wiringPiSetup() == -1)
		exit(EXIT_FAILURE);
	if (setuid(getuid()) < 0)
	{
		perror("Dropping privileges failed\n");
		exit(EXIT_FAILURE);
	}

	pthread_t p, c;
	pthread_mutex_init(&mutex, NULL);

	pthread_create(&p, NULL, producer, "P");
	pthread_create(&c, NULL, consumer, "C");

	pthread_join(p, NULL);
	pthread_join(c, NULL);
	
	return 0;
}

int read_dht22_dat_temp()
{
	uint8_t laststate = HIGH;
	uint8_t counter = 0;
	uint8_t j = 0,  i;

	dht22_dat[0] = dht22_dat[1] = dht22_dat[2] = dht22_dat[3] = dht22_dat[4] = 0;

	pinMode(DHTPIN, OUTPUT);
	digitalWrite(DHTPIN, HIGH);
	delay(10);
	digitalWrite(DHTPIN, LOW);
	delay(18);

	digitalWrite(DHTPIN, HIGH);
	delayMicroseconds(40);

	pinMode(DHTPIN, INPUT);

	for (i=0; i<MAXTIMINGS; i++) {
		counter = 0;
		while (sizecvt(digitalRead(DHTPIN)) == laststate) {
			counter++;
			delayMicroseconds(1);
			if (counter == 255) {
				break;
			}
		}
		laststate = sizecvt(digitalRead(DHTPIN));

		if (counter == 255) break;

		if ((i >= 4) && (i%2 == 0)) {
			dht22_dat[j/8] <<= 1;
			if(counter > 50)
				dht22_dat[j/8] |= 1;
			j++;
		}
	}

	if((j >= 40) && (dht22_dat[4] == ((dht22_dat[0] + dht22_dat[1] + dht22_dat[2] + dht22_dat[3]) & 0xFF)) ) {
		float t, h;

		h = (float)dht22_dat[0] * 256 + (float)dht22_dat[1];
		h /= 10;
		t = (float)(dht22_dat[2] & 0x7F) * 256 + (float)dht22_dat[3];
		t /= 10.0;
		if ((dht22_dat[2] & 0x00) != 0) t *= -1;

		ret_humid = (int)h;
		ret_temp = (int)t;

		return ret_temp;
	}
	else 
	{
		return 0;
	}
}
int get_temperature_sensor()
{
	int received_temp;

	DHTPIN = 11;

	if(wiringPiSetup() == -1)
		exit(EXIT_FAILURE);
	if(setuid(getuid()) < 0)
	{
		perror("Dropping privileges failed\n");
		exit(EXIT_FAILURE);
	}
	while (read_dht22_dat_temp() == 0)
	{
		delay(3000);
	}
	received_temp = ret_temp;
	printf("Temperature = %d\n", received_temp);

	return received_temp;
}


