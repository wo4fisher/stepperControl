/* controlAlgorithm.c */
#include <controlAlgorithm.h>

int32_t pulsCnt1 = 0;
int32_t pulsCnt2 = 0;
#ifdef FULL_SETUP
int32_t pulsCnt3 = 0;
int32_t pulsCnt4 = 0;
#endif 

/**
	* \brief This function is implements stepper motor controller. Controller 
	* 			 is proportional with rate rate limiting.
	*	\param <*pvParameters> In task creation pass the number of the motor (value: 1 .. 4)
	*/
	
void vTaskMotorController(void *pvParameters)
{
	//Default controler params
	int32_t P = 2;
	int32_t rLim = (50*100/CONTROL_LOOP_FREQUENCY);
	int32_t dLim	=	(50*100/CONTROL_LOOP_FREQUENCY);
	int32_t wMax	= 1500;
	
	//Position and angular velocity 
	int32_t stepAct  = 0;			//Actual step count of motor [pulse]
	int32_t stepRef  = 0;			//Setpoint value 
	
	int32_t wAct		 = 0;			//Actual speed that the motor will achive [pps]
	int32_t wRef		 = 0;			//Output from proportinal regulator, before rate limiting [pps]
	int32_t wNew		 = 0;			//New speed for controller 
	int32_t e				 = 0;			//Control error signal
	TickType_t xLastWakeTime = xTaskGetTickCount();			//For vTaskDelay function 
	motor_setup_t setupMsg;															//Container for received msg from motor setup queue
	uint32_t setpointMsg;																//Container for received msg from motor setpoint queue 
	
	
	//helper variables
	int32_t delta = 0;						//Time delay between output compare ISR executions expresed in timer ticks, 
																// -1 value signals that the motor should not move
	int16_t   direction = 0;			//1 - forward, -1 - backward, 0 - stop
	
	//Message structure for ISR queue
	ISR_message_t msg;
	
	//Motor number is passed as argument in xTaskCreate function. 
	//This enables to use one function with up to 4 tasks 
	uint32_t motorNumber = (uint32_t)pvParameters;
	//Program execution is stopped if argument passed to task is > 4
	configASSERT( motorNumber <= 4 );
	
	//infinite controller loop 
	while(1)
	{
		//For control loop timing vTaskDelayUntil function is used for convenience and 
		// it offers sufficient time accuracy
		vTaskDelayUntil(&xLastWakeTime,2/portTICK_PERIOD_MS);
		
		//Checking if there are new regulator setpoints in the setpoint queue
		if(xQueueReceive(xQueueMotorSetup,(void *)&setupMsg,(TickType_t) 0) == pdTRUE)
		{
			//Refresh controler parameters
			P = setupMsg.P;
			wMax = setupMsg.wMax;
			rLim = setupMsg.rLim;
			dLim = setupMsg.dLim;
		}
		//Checking if there is new setpoint available in the queue 
		if(xQueueReceive( xQueueMotorSetpoint[motorNumber-1], (void *)&setpointMsg, (TickType_t) 0) == pdTRUE)
		{
			stepRef = setpointMsg;
		}
		
		//Control algorithm (Proportinal regulator with rate limiting
		//Get current step count
		stepAct = getPulsCnt(motorNumber);
		//Control error signal
		e = stepRef-stepAct;
		//referent velocity 
		wRef = e*P;
		
		//rate limiting 
		if((wRef-wAct)>rLim)
		{
			wNew = wAct+rLim;
		}
		else if((wAct - wRef) > dLim)
		{
			wNew = wAct-dLim;
		}
		else
		{
			wNew = wRef;
		}
		
		//Saturation
		if(absVal(wNew) > wMax)
		{
			if(sign(wNew) == -1)
			{
				wNew = -1*wMax;
			}
			else if(sign(wNew)==1)
			{
				wNew = wMax;
			}
		}
		
		//Calculate delay between steps 
		if(wNew == 0)
		{
			direction = 0;
			delta = -1;			// Signals ISR that motor needs to be stoped 
		}
		else
		{
			delta = (PERIOD/(2*absVal(wNew)));	//2 because it has to turn the step pin on and off in real delta time
			direction = sign(wNew);
		}
		
		//Send to timer output compare ISR via correct motor queue 
			//Form a message
			msg.delta 		 = delta;
			msg.direction = direction;
			//Add to Queue
			xQueueOverwrite(xQueueMotorISR[motorNumber-1],
												(void *)&msg);			
													
		//Set new compare on speed change for fast response
			if(wNew != wAct)
			{
				//Determine which timer chanel output compare to set 
				switch (motorNumber)
				{
					case (1) : 
					{
						TIM_SetCompare1(TIM4,(TIM_GetCounter(TIM4)+delta)%PERIOD);
						break;
					}
					case (2) :
					{
						TIM_SetCompare2(TIM4,(TIM_GetCounter(TIM4)+delta)%PERIOD);
					}
					case (3) :
					{
						TIM_SetCompare3(TIM4,(TIM_GetCounter(TIM4)+delta)%PERIOD);
						break;
					}
					case (4) :
					{
						TIM_SetCompare4(TIM4,(TIM_GetCounter(TIM4)+delta)%PERIOD);
					}
					default  :
						break;
				}
			}
		//save new speed as actual speed for next step 
			wAct = wNew;
	}
	
	
}	

/**
	*\brief This function calculates absoulute value of integer number 
	*
	*\param <value> integer number
	*
	*\retval absolute value of input argument 
	*/

int32_t absVal(int32_t value)
{
	if(value < 0)
	{
		return (int32_t)(-1*value);
	}
	else
	{
		return (int32_t)value;
	}
}

/**
	*\brief This function determines sign of integer number 
	*
	*\param <value> integer number
	*
	*\retval sign of input vlue. -1 for negative number, 1 for positive and 0 for 0 input value. 
	*/

int16_t sign(int32_t value)
{
	if(value < 0)
	{
		return (-1);
	}
	else if (value > 0)
	{
		return (1);
	}
	else
	{
		return (0);
	}
}
/**
	*	\brief Timer 4 interrupt serivce routine. This function executes
	* 			 on output compare event for each of the 4 chanels of timer TIM4.
	*
	*				 This function sets new output compare value for timer channel that 
	*				 caused the interrupt
	*
	*/

void TIM4_IRQHandler(void)
{
	static BitAction bitValue = Bit_RESET;
	static BitAction bitValue2 = Bit_RESET;
	static int32_t delta			= PERIOD;
	static int32_t delta2			= PERIOD;
	static int16_t direction2 = 0;
	static int16_t		direction = 0;
	BaseType_t xTaskWokenByReceive = pdFALSE;
	ISR_message_t	rxMsg;
	
	//Check if CC1 caused interrupt 
	
	if(TIM_GetITStatus(TIM4,TIM_IT_CC1) != RESET)
	{
		
		//See if there is a new message in the queue
		if(xQueueReceiveFromISR(xQueueMotorISR[0], (void *)&rxMsg, &xTaskWokenByReceive) == pdTRUE)
		{
			delta 		= rxMsg.delta;
			direction = rxMsg.direction;
		}
		//If the time interval is valid toggle the step pin 
		if(delta != -1)
		{
			bitValue = (bitValue == Bit_RESET) ? Bit_SET : Bit_RESET;
			GPIO_WriteBit(GPIOD , GPIO_Pin_12, bitValue);															//<------------- delete this in final form 
			GPIO_WriteBit(MOTOR1_GPIOx, MOTOR1_STEP, bitValue);
			
			//Increment step counter, when direction is 0 there is no increment 
			if(bitValue == Bit_SET)
			{
				if(direction == 1)
				{
					pulsCnt1 = pulsCnt1 + 1;
				}
				else if (direction == -1)
				{
					pulsCnt1 = pulsCnt1 - 1;
				}
			}
		}
			
		//Set direction pin properly
		if(direction == 1)
		{
			GPIO_WriteBit(MOTOR1_GPIOx, MOTOR1_DIR, Bit_RESET);
		}
		else if(direction == -1)
		{
			GPIO_WriteBit(MOTOR1_GPIOx, MOTOR1_DIR, Bit_SET);
		}
						
		//Flash blue light just for confirmation
		GPIO_ToggleBits(GPIOD, GPIO_Pin_15);
		//Set next OC ISR time 
		uint32_t curTim = TIM_GetCounter(TIM4);
		if (delta != -1)
		{
			TIM_SetCompare1(TIM4,(curTim+delta)%PERIOD);
		}
		else
		{
			TIM_SetCompare1(TIM4,(curTim+PERIOD)%PERIOD);
		}
		//Clear pending bit
		TIM_ClearITPendingBit(TIM4, TIM_IT_CC1);
	}
	
	//Check if CC2 caused interupt 
	if(TIM_GetITStatus(TIM4,TIM_IT_CC2) != RESET)
	{
		if(xQueueReceiveFromISR(xQueueMotorISR[1], (void *)&rxMsg, &xTaskWokenByReceive) == pdTRUE)
		{
			delta2 		= rxMsg.delta;
			direction2 = rxMsg.direction;
		}
		
		if(delta2 != -1)
		{
			bitValue2 = (bitValue2 == Bit_RESET) ? Bit_SET : Bit_RESET;
			GPIO_WriteBit(MOTOR2_GPIOx, MOTOR2_STEP, bitValue2);
			GPIO_WriteBit(GPIOD , GPIO_Pin_13, bitValue);	
			
			if(bitValue2 == Bit_RESET)
			{
				if(direction2 == 1)
				{
					pulsCnt2 = pulsCnt2 + 1;
				}
				else if (direction2 == -1)
				{
					pulsCnt2 = pulsCnt2 - 1;
				}
			}
		}
		
		//Set direction pin properly
		if(direction2 == 1)
		{
			GPIO_WriteBit(MOTOR2_GPIOx , MOTOR2_DIR , Bit_RESET);
		}
		else if(direction2 == -1)
		{
			GPIO_WriteBit(MOTOR2_GPIOx , MOTOR2_DIR , Bit_SET);
		}
		
		//Set next OC ISR time 
		uint32_t curTim = TIM_GetCounter(TIM4);
		if (delta2 != -1)
		{
			TIM_SetCompare2(TIM4,(curTim+delta2)%PERIOD);
		}
		else
		{
			TIM_SetCompare2(TIM4,(curTim+PERIOD)%PERIOD);
		}
		//Clear pending bit
		TIM_ClearITPendingBit(TIM4, TIM_IT_CC2);
		
	}
	//Call task scheduler if there is a task unblocked by reading from queue 
	//In this project it isn't necessary but we included it for completeness
	if( xTaskWokenByReceive != pdFALSE)
		{
			 portYIELD_FROM_ISR( xTaskWokenByReceive );
		}
}

/**
	* \brief  This function returns the number of steps that the motor has done.
	*					Because pulsCntx is a global variable, reading it's value was done 
	*					in a critical section.
	*	\param  <num> number of motor we want to get the step count for 
	*
	* \retval Number of steps the stepper motor has traveled
	*
	*/
int32_t getPulsCnt(uint32_t num)
{
	int32_t ret = 0;
	NVIC_DisableIRQ(TIM4_IRQn);
	if(num == 1)
	{
		ret = pulsCnt1;
	}
	if(num == 2)
	{
		ret = pulsCnt2;
	}
	#ifdef FULL_SETUP
	if(num == 3)
	{
		ret = pulsCnt3;
	}
	if(num == 4)
	{
		ret = pulsCnt4;	
	}
	#endif
	NVIC_EnableIRQ(TIM4_IRQn);
	return ret;
}