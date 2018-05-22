#include <wiringPi.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <wiringPiSPI.h>

#include <softPwm.h>

#include <mysql/mysql.h>

#include <time.h>
#include <math.h>

#include <pthread.h>

#define MAX 5

#define CS_MCP3208 11
#define SPI_CHANNEL 0
#define SPI_SPEED 1000000

#define MAXTIMINGS 85

#define RGBLEDPOWER 24
#define RED 7
#define GREEN 9
#define BLUE 8

#define FAN 22

#define LIGHTSEN_OUT 2

int read_dht22_dat();
int get_temperature_sensor();
int get_light_sensor();

#define DBHOST "localhost"
#define DBUSER "root"
#define DBPASS "root"
#define DBNAME "demofarmdb"

MYSQL *connector;
MYSQL_RES *result;
MYSQL_ROW row;

int ret_humid, ret_temp;

static int DHTPIN = 11;

int buffer[MAX];
int fill_ptr = 0;
int use_ptr = 0;
int count = 0;

void put(int value) {
	buffer[fill_ptr] = value;
	fill_ptr = (fill_ptr + 1) %MAX;
	count ++;
}

int get() {
	int tmp = buffer[use_ptr];
	use_ptr = (use_ptr + 1) % MAX;
	count--;

	int adcChannel = 0;
	int adcValue[1];

	connector = mysql_init(NULL);
	if (!mysql_real_connect(connector, DBHOST, DBUSER, DBPASS, DBNAME, 3306, NULL, 0))
	{
		fprintf(stderr, "%s\n", mysql_error(connector));
		return 0;
	}

//	printf("MySQL(rpidb) opened\n");

//	while(1)
//	{
		char query[1024];
		adcValue[0] = get_temperature_sensor();
		adcValue[1] = get_light_sensor();

		sprintf(query, "insert into thj values (now(),%d,%d)", adcValue[0], adcValue[1]);

		if(mysql_query(connector, query))
		{
			fprintf(stderr, "%s\n", mysql_error(connector));
			printf("Write DB error\n");
		}
	//	delay(1000);
//	}
	mysql_close(connector);

	return tmp;
}

pthread_cond_t empty, fill;
pthread_mutex_t mutex;

void *producer1(void *arg) {
	int i;
	for(i = 0; i<10; i++) {
		pthread_mutex_lock(&mutex);
		while (count == MAX)
			pthread_cond_wait(&empty, &mutex);

		int temper = read_dht22_dat();
		put(temper);

		pthread_cond_signal(&fill);
		pthread_mutex_unlock(&mutex);
	}
}


void *consumer1(void *arg){
	int i;
	for(i = 0; i<10; i++) {
		pthread_mutex_lock(&mutex);
		while(count == 0)
			pthread_cond_wait(&fill, &mutex);

		int tmp = get();

		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&mutex);
	}
}



int read_mcp3208_adc(unsigned char adcChannel)
{
	unsigned char buff[3];
	int adcValue = 0;

	buff[0] = 0x06 | ((adcChannel & 0x07) >> 2);
	buff[1] = ((adcChannel & 0x07) << 6);
	buff[2] = 0x00;

	digitalWrite(CS_MCP3208, 0);

	wiringPiSPIDataRW(SPI_CHANNEL, buff, 3);

	buff[1] = 0x0F & buff[1];
	adcValue = ( buff[1] << 8) | buff[2];

	digitalWrite(CS_MCP3208, 1);

	return adcValue;
}

static int dht22_dat[5] = {0,0,0,0,0};


void sig_handler(int signo)
{
	printf("Process stop \n");
	digitalWrite(RED, 0);
	digitalWrite(GREEN, 0);
	digitalWrite(BLUE, 0);
	digitalWrite(RGBLEDPOWER, 0);
	digitalWrite(FAN, 0);

	exit(0);
}

int main (void)
{
	if(wiringPiSetup() == -1)
	{
		fprintf(stdout, "Unable to start wiringPi: %s\n", strerror(errno));
		return 1;
	}

	if(wiringPiSetup() == -1)
		exit(EXIT_FAILURE);
	if (setuid(getuid()) <0)
	{
		perror("Dropping privileges failed\n");
		exit(EXIT_FAILURE);
	}

	
	pthread_t p1, c1;
	pthread_mutex_init(&mutex, NULL);

	pthread_create(&p1, NULL, producer1, "P1");
	pthread_create(&c1, NULL, consumer1, "C1");

	pthread_join(p1, NULL);
	pthread_join(c1, NULL);
	

	return 0;
}

static uint8_t sizecvt(const int read)
{
	if (read > 255 || read <0)
	{
		printf("Invaild data from wiringPi library\n");
		exit(EXIT_FAILURE);
	}
	return (uint8_t)read;
}

int read_dht22_dat()
{
	uint8_t laststate = HIGH;
	uint8_t counter = 0;
	uint8_t j = 0, i;

	dht22_dat[0] = dht22_dat[1] = dht22_dat[2] = dht22_dat[3] = dht22_dat[4] = 0;

	pinMode(DHTPIN, OUTPUT);
	digitalWrite(DHTPIN, HIGH);
	delay(10);
	digitalWrite(DHTPIN, LOW);
	delay(18);

	digitalWrite(DHTPIN, HIGH);
	delayMicroseconds(40);

	pinMode(DHTPIN, INPUT);

	for (i = 0; i<MAXTIMINGS; i++) {
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


int wiringPicheck(void)
{
	if(wiringPiSetup() == -1)
	{
		fprintf(stdout, "Unable to start wiringPi: %s\n", strerror(errno));
		exit(1);
	}
}


int get_temperature_sensor()
{
	int received_temp;

	signal(SIGINT, (void *)sig_handler);

	if(wiringPiSetup() == -1)
	{
		fprintf(stdout, "Unable to start wiringPi: %s\n", strerror(errno));
		return 1;
	}

	pinMode(FAN, OUTPUT);

	
	DHTPIN = 11;

	received_temp = ret_temp;

	if(wiringPiSetup() == -1)
	{
		perror("Dropping privilegies failed\n");
		exit(EXIT_FAILURE);
	}

	while (read_dht22_dat() == 0)
	{
		delay(500);
	}

	if(received_temp >= 28)
	{
		digitalWrite(FAN, 1);
	}

	else
	{
		digitalWrite(FAN, 0);
	}
	printf("Temperature = %d\n", received_temp);
	return received_temp;

}
int get_light_sensor()
{
	signal(SIGINT, (void *)sig_handler);
	unsigned char adcChannel_light = 0;
	int adcValue_light = 0;

	float vout_light;
	float vout_oftemp;
	float percentrh = 0;
	float supsiondo = 0;

	pinMode(RGBLEDPOWER, OUTPUT);
	pinMode(RED, OUTPUT);
	pinMode(GREEN, OUTPUT);
	pinMode(BLUE, OUTPUT);

	pinMode(CS_MCP3208, OUTPUT);

	adcValue_light = read_mcp3208_adc(adcChannel_light);
	printf("light sensor = %d\n", adcValue_light);

	delay(500);

	if(adcValue_light >=  0)
	{
		digitalWrite(RED, 1);
	}
	else
	{
		digitalWrite(RED, 0);
		digitalWrite(RGBLEDPOWER, 0);
	}
	return adcValue_light;

}
