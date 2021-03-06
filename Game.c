#include <stdint.h>
#include "Game.h"
#include "tm4c123gh6pm.h"

uint16_t score,life,activeBullets;
shield* shields[NUMSHIELDS];
//bullet* bullets[10];	//Max bullets stored 10
shield Shield1,Shield2,Shield3;
player player1;


void initGame() {
	int i;
	score = 0;
	life = 5;
	activeBullets = 0;
	shields[0] = &Shield1;
	shields[1] = &Shield2;
	shields[2] = &Shield3;
	for(i = 0;i < NUMSHIELDS;i++) {
		shields[i]->position[0] = 19+i*(16+20);	// Used to define equal spacing between shields
		shields[i]->position[1] = 85;
		shields[i]->life = 3;
		shields[i]->notHit = 1;
	}
}
/*
void fireBullet(int16_t x, int16_t y){
	bullet newBullet;
	newBullet.position[0] = player1.position[0];
	newBullet.position[1] = player1.position[0];
	bullets[activeBullets] = &newBullet;
	activeBullets++;
}
*/
void drawPlayer(uint16_t* ssImage){
	if(player1.position[0] != player1.old_position[0]){
		BSP_LCD_FillRect(player1.old_position[0],100,15,15,LCD_BLACK);
		//BSP_LCD_FillRect(player1.position[0],100,10,10,LCD_GREEN);
		BSP_LCD_DrawBitmap(player1.position[0],100+14, ssImage, 15,15);
		player1.old_position[0] = player1.position[0];
	}
}