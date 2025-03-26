#include "rc522.h"

u8 rc522_trans_recv(u8 data){
	u8 data_receive;
	HAL_SPI_TransmitReceive(&hspi1, &data, &data_receive, 1, 1000);
	return data_receive;
}

void trans_rc522(u8 address, u8 value){
	HAL_GPIO_WritePin(CS_PORT, CS_PIN, 0);
	// Theo datasheet thi bit 7 : 0/1 <=> W/R
	// bit 6-1 : Gui gia tri thanh ghu
	// bit 0: Luon la khong theo yeu cau cua rc522
	rc522_trans_recv((address<<1)&0x7E);	// Tro vao thanh ghi can giao tiep
	rc522_trans_recv(value); // Gui data can gui vao thanh ghi vua tro vao
	HAL_GPIO_WritePin(CS_PORT, CS_PIN, 1);
}

u8 read_rc522(u8 address){	// Doc val tu 1 thanh ghi da tro vao
	u8 val;
	HAL_GPIO_WritePin(CS_PORT, CS_PIN, 0);
	rc522_trans_recv(((address<<1)&0x7E)|0x80);  // Do day la can doc nen chung ta set bit 7 len 1
	val = rc522_trans_recv(0x00);
	HAL_GPIO_WritePin(CS_PORT, CS_PIN, 1);
	return val;
}

void set_bit_mask(u8 reg, u8 mask){
	u8 save = read_rc522(reg);
	trans_rc522(reg, save|mask);
}

void clear_bit_mask(u8 reg, u8 mask){
	u8 save = read_rc522(reg);
	trans_rc522(reg, save&(~mask));
}

void on_aten(void){
	u8 temp = read_rc522(TxControlReg);
	set_bit_mask(TxControlReg, 0x03);
	if(!(temp&0x03)){
		set_bit_mask(TxControlReg, 0x03);
	}
}

void off_aten(void){
	clear_bit_mask(TxControlReg, 0x03);
}

void soft_reset(void){
	trans_rc522(CommandReg, 0x0f);	// theo datasheet
}

void rc522_init(void){
	HAL_GPIO_WritePin(CS_PORT, CS_PIN, 1);
	HAL_GPIO_WritePin(RST_PORT,RST_PIN,1);
	soft_reset();
	trans_rc522(TModeReg, 0x8D); // Lay [0-3]
	trans_rc522(TPrescalerReg, 0x3E); // Lay ca 8 => Pres = 3390
	trans_rc522(TReload_high, 0x1E);
	trans_rc522(TReload_low, 0x00); // -> Reload = 30
	trans_rc522(TxASKReg, 0x40);
	trans_rc522(ModeReg, 0x3D);
	on_aten();
}

u8 rc522_to_card(u8 command, u8* data_trans, u8 size_data_trans, u8* data_recv, u16* size_data_recv){
	u8 save_state = 2; 			//2: Error , 1: Notager , 0: OK
	u8 IRQ_E = 0x00; 				// Interupt request enable -> Cac ngat se bat
	u8 wait_IRQ_E = 0x00;		// Cac ngat se cho
	u8 last_bit;						// Cac bit le cuoi cung
	u8 n;										// Byte nhan duoc tu FIFO
	u16 i;
	// Cau hinh ngat
	if(command == CollReg){
		IRQ_E = 0x12; 				// Reg 0x02
		wait_IRQ_E = 0x10;
	}else
	if(command == ControlReg){
		IRQ_E = 0x77;
		wait_IRQ_E = 0x30;
	}else;
	
	// Set up
	trans_rc522(ComlEnReg, IRQ_E|0x80) ; 	// Theo datasheet can active low
	clear_bit_mask(ComIrqReg, 0x80); 			// Xoa co ngat
	set_bit_mask(FIFOLevelReg, 0x80); 
	
	trans_rc522(CommandReg, 0x00);				// Dung lai cac hoat dong
	for(u16 i=0; i<size_data_trans; i++){
		trans_rc522(FIFODataReg, data_trans[i]);
	}
	
	trans_rc522(CommandReg, command);
	if(command == ControlReg){
		set_bit_mask(BitFramingReg, 0x80);
	}
	
	i=2000;
	do{
		n = read_rc522(CommIrqReg);
		i--;
	}while(i != 0 && !(n&0x01) && !(n&wait_IRQ_E)); 	// Cho cho den khi ngat xay ra
	
	clear_bit_mask(BitFramingReg, 0x80); // 0b0000 0000 => dung gui
	
	if(i==0){
		save_state = 2; // false
		return save_state;
	}else{
		if(!(read_rc522(ErrorReg)& 0x1B)){
			save_state = 0; // OK
			if(n & IRQ_E & 0x01){
				save_state = 1; // ...
			}
			if (command == ControlReg)
			{
				n = read_rc522(FIFOLevelReg);
				last_bit = read_rc522(ControlReg) & 0x07;
				if (last_bit)
				{   
					*size_data_recv = (n-1)*8 + last_bit;   
				}
				else
				{   
					*size_data_recv = n*8;   
				}

			if (n == 0)
			{   
				n = 1;    
			}
			if (n > 16)
			{   
				n = 16;   
			}

//FIFO doc in the received data
			for (i=0; i<n; i++)
			{   
				data_recv[i] = read_rc522(FIFODataReg);    
			}
			}
		}
	}
	
	return save_state;
}

u8 rc522_anticol(u8 *serNum){
	u8 status;
	u8 i;
	u8 serNumCheck=0;
	u16 unLen;
    
	trans_rc522(BitFramingReg, 0x00);		//TxLastBists = BitFramingReg[2..0]
 
	serNum[0] = PICC_ANTICOLL;
	serNum[1] = 0x20;
	status = rc522_to_card(ControlReg, serNum, 2, serNum, &unLen);

    if (status == 0)
	{
		//Check the serial number
		for (i=0; i<4; i++)
		{   
		 	serNumCheck ^= serNum[i];
		}
		if (serNumCheck != serNum[i])
		{   
			status = 2;    
		}
    }

    return status;
}
u8 rc522_request(uint8_t reqMode, uint8_t *TagType){
	uint8_t status;  
	u16 backBits;

	trans_rc522(BitFramingReg, 0x07);
	
	TagType[0] = reqMode;
	status = rc522_to_card(ControlReg, TagType, 1, TagType, &backBits);

	if ((status != 0) || (backBits != 0x10))
	{    
		status = 2;
	}
   
	return status;
}

