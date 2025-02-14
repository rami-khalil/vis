#include "targeter.h"

void loadConfig() {
	freopen("Data/positioner.conf", "r", stdin);
	cin >> cparaname >> targets;
	target = new marker[targets];
	for(int i = 0; i < targets; i++) {
		cin >> target[i].patternFile;
		cin >> target[i].center[0] >> target[i].center[1] >> target[i].width;
		for(int j = 0; j < TRANS_MAT_ROWS; j++)
			for(int k = 0; k < TRANS_MAT_COLS; k++)
				cin >> target[i].transformation[j][k];
		target[i].measurements = 0;

		for(int j = 0; j < QUAT_DIMS; j++)
			target[i].quaternion_rot[j] = 0;
		for(int j = 0; j < POS_DIMS; j++)
			target[i].position[j] = 0;

		target[i].filter = arFilterTransMatInit(AR_FILTER_TRANS_MAT_SAMPLE_RATE_DEFAULT, AR_FILTER_TRANS_MAT_CUTOFF_FREQ_DEFAULT);
		
		target[i].trick_adjustment_filter = arFilterTransMatInit(AR_FILTER_TRANS_MAT_SAMPLE_RATE_DEFAULT, AR_FILTER_TRANS_MAT_CUTOFF_FREQ_DEFAULT);
		target[i].viable = false;
	}
	transFilter = arFilterTransMatInit(AR_FILTER_TRANS_MAT_SAMPLE_RATE_DEFAULT, AR_FILTER_TRANS_MAT_CUTOFF_FREQ_DEFAULT/3);
	target[0].measurements = SAMPLES + 1;
}

int detectMarkers() {
    static int      contF2 = 0;
	ARUint8         *dataPtr;
    int             imageProcMode;
    int             debugMode;
    //ARdouble        err;

    /* grab a vide frame */
    if( (dataPtr = (ARUint8 *)arVideoGetImage()) == NULL ) {
        arUtilSleep(2);
        return -1;
    }

	argDrawMode2D(vp);
    arGetDebugMode( arHandle, &debugMode );
    if( debugMode == 0 ) {
        argDrawImage( dataPtr );
    }
    else {
        arGetImageProcMode(arHandle, &imageProcMode);
        if( imageProcMode == AR_IMAGE_PROC_FRAME_IMAGE ) {
            argDrawImage( arHandle->labelInfo.bwImage );
        }
        else {
            argDrawImageHalf( arHandle->labelInfo.bwImage );
        }
    }

    /* detect the markers in the video frame */
	//argDispImage( dataPtr, 0, 0 );

    if( arDetectMarker(arHandle, dataPtr) < 0 ) {
        cleanup();
        exit(0);
    }

    marker_num = arGetMarkerNum( arHandle );
    //if( !marker_num ) {
    //    return 0;
	//}

    /* grab marker transformations */
    marker_info =  arGetMarker( arHandle ); 
	
	for (int i = 0; i < targets; i++)
		target[i].validPrev = target[i].valid,
		target[i].valid = false,
		target[i].idx = -1;

	recognized_targets = 0;
	measured_recognized_num = 0;
	sort(marker_info, marker_info + marker_num, marker_comparison);
	for (int i = 0; i < marker_num; i++) {
		int id = marker_info[i].id;
		if (marker_info[i].cf > CONFIDENCE_THRESHOLD && target_set.count(id) && target[id].idx == -1) {
			target[id].idx = i;
			showId(target[id]);
			recognized_targets++;
			if (target[id].measurements > SAMPLES)
				measured_recognized_num++;
			getResultRaw(&marker_info[i], target[id].marker_trans, target[id].marker_trans_inv);
		}
	}

	// HUD info
	if (count_ar % 75 == 0) {
		sprintf(fps, "%f[fps] %d/%d markers", 75.0 / arUtilTimer(), recognized_targets, marker_num);
		arUtilTimerReset();
	}
	count_ar++;
	glColor3f(0.0f, 1.0f, 0.0f);
	argDrawStringsByIdealPos(fps, 10, ysize - 30);

	return marker_num;
}

void mainLoopTargeter() {
	int result = detectMarkers();
	if (result == -1) {
		arUtilSleep(2);
		return;
	} else if(result == 0) {
		argSwapBuffers();
		return;
	}

	int cnt = inferPosition();
	//int cnt = inferPositionFancy();

	/*
	if (cnt) {
		updatePositionS(trans);
	}
	*/
	
	/*
	if(cnt && connected) {
		double **tmp = new double*[3];
		for(int i = 0; i < 3; i++)
			tmp[i] = new double[4];
		memcpy(tmp, trans, sizeof trans);
		thread luigi(updatePosition, tmp);
		luigi.detach();
	}
	*/


    argSwapBuffers();
}

int inferPosition() {
	int cnt = 0;
	double quaternion_rot[QUAT_DIMS], position[POS_DIMS];
	memset(quaternion_rot, 0, sizeof quaternion_rot);
	memset(position, 0, sizeof position);

	for (int i = 0; i < marker_num; i++) {
		int id = marker_info[i].id;
		glColor3f(1.0f, 0.0f, 0.0f);
		if (id != -1 && target[id].idx == i && (target[id].measurements > SAMPLES || runMode == RUN_MODE_POSITIONER)) {
			glColor3f(1.0f, 1.0f, 0.0f);

			if (!agreeWithMajority(id))
				continue;

			if (!saneMatrix(target[id].inferred_position) || !saneMatrix(target[id].marker_trans_inv))
				continue;

			if (!transformAverageAdd(target[id].inferred_position, position, quaternion_rot))
				continue;

			/*
			if(runMode == RUN_MODE_POSITIONER) {
				printf("%d (%f) Says:\n", id, marker_info[i].cf),
				printMat(target[id].transformation);
				printMat(target[id].marker_trans_inv);
				printMat(target[id].inferred_position);
			}
			*/

			cnt++;
			glColor3f(0.0f, 1.0f, 0.0f);
			draw(target[id].marker_trans);
		}
	}

	double inferred[3][4];
	if(cnt) {
		transformAverageNormalize(inferred, position, quaternion_rot, cnt);

		/*if(runMode == RUN_MODE_POSITIONER)
			printf("%d Markers Say:\n", cnt),
			printMat(inferred);
			*/
		applyTrans(inferred);
	} else if(runMode == RUN_MODE_POSITIONER) {
		// Take what we can get!
		double conf = 0; int mostConf = -1;
		for (int i = 0; i < marker_num; i++) {
			int id = marker_info[i].id;
			if (id != -1 && target[id].idx == i && marker_info[i].cf > conf)
				if (saneMatrix(target[id].inferred_position) && saneMatrix(target[id].marker_trans_inv))
					conf = marker_info[i].cf, mostConf = id;
//				else
//					cout << "Could only get insane matrix from " << id << endl;
		}

		if (mostConf != -1) {
//			cout << "Taking what i can get! " << mostConf << endl;
			glColor3f(.7f, .7f, .1f);
			draw(target[mostConf].marker_trans);
			applyTrans(target[mostConf].inferred_position);
		}
	}

	return cnt;
}

void idTrans(ARdouble trans[3][4]) {
	for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++)
		trans[i][j] = i == j ? 1.0 : 0.0;
}

int inferPositionFancy() {
	//ARdouble inferred[3][4], tmp[3][4], tmp2[3][4];
	//static ARdouble discovery[3][4], transition[3][4], adjustment[3][4];
	static int lastKnown;

	/*ARdouble testA[3][4], testB[3][4];
	idTrans(testA);
	idTrans(testB);
	testB[1][3] = 5;
	arUtilMatMul(testB, testA, tmp);
	printMat(testB);
	printMat(testA);
	printMat(tmp);
	cout << "====================" << endl;*/

	if (!transValid) {
		lastKnown = 0;
		//idTrans(discovery);
		//idTrans(transition);
	}

	int lowestAvailable = targets, canInfer = false;
	for (int i = 0; i < marker_num; i++) {
		int id = marker_info[i].id;
		/*cerr << "-----------------" << endl;
		cerr << i << ' ' << id << ' ' << target[id].idx << endl;
		cerr << saneMatrix(target[id].marker_trans) << ' ' << saneMatrix(target[id].marker_trans_inv) << endl;
		cerr << "-----------------" << endl;*/
		if (id == -1 || target[id].idx != i) continue;
		if (!saneMatrix(target[id].marker_trans) || !saneMatrix(target[id].marker_trans_inv)) continue;
		//if (id != 0 && !target[id].viable) continue;

		if (id == lastKnown) canInfer = true;
		lowestAvailable = min(lowestAvailable, id);
	}

	/*if (canInfer) {
		for (int i = 0; i < marker_num; i++) {
			int id = marker_info[i].id;
			if (id == -1 || target[id].idx != i) continue;
			if (!saneMatrix(target[id].marker_trans) || !saneMatrix(target[id].marker_trans_inv)) continue;
			if (!id || id == lastKnown) continue;

			arUtilMatMul(target[lastKnown].marker_trans, target[id].marker_trans_inv, target[id].trick_adjustment);
			if (arFilterTransMat(target[id].trick_adjustment_filter, target[id].trick_adjustment, !target[id].viable) < 0)
				ARLOGe("arFilterTransMat error with camera transform.\n");
			target[id].viable = true;
		}
		for (int i = 0; i < targets; i++)
			if (target[i].id && target[i].idx == -1)
				target[i].viable = false;
	} else*/ if (lowestAvailable == 0) {
		//idTrans(discovery);
		//idTrans(transition);
		lastKnown = 0;
		canInfer = true;
	} else if (!canInfer && lowestAvailable != targets) {
		/*memcpy(transition, trans, sizeof transition);
		memcpy(discovery, target[lowestAvailable].marker_trans, sizeof discovery);*/
		lastKnown = lowestAvailable;
		canInfer = true;
	}

	if (canInfer) {
		/*arUtilMatMul(target[lastKnown].marker_trans_inv, discovery, tmp);
		arUtilMatMul(transition, tmp, inferred);*/

		glColor3f(0.0f, 1.0f, 0.0f);
		draw(target[lastKnown].marker_trans);
		applyTrans(target[lastKnown].inferred_position);

		//cerr << lastKnown << endl;
	}
	else {
		//cerr << "NONE! " << lowestAvailable << ' ' << marker_num << endl;
	}

	return canInfer;
}

void getResultRaw( ARMarkerInfo *marker_info, double xyz[3][4] , double mxyz[3][4] )
{
	int id = marker_info->id;

	if (target[id].validPrev)
		target[id].error = arGetTransMatSquareCont(ar3DHandle, marker_info, target[id].marker_trans, target[id].width, xyz);
	else
		target[id].error = arGetTransMatSquare(ar3DHandle, marker_info, target[id].width, xyz);
	target[id].valid = true;

	if (target[id].filter)
		if (arFilterTransMat(target[id].filter, target[id].marker_trans, !target[id].validPrev) < 0)
			ARLOGe("arFilterTransMat error with marker %d.\n", id);
			

    if( arUtilMatInv(xyz, mxyz) < 0 ) return;

	if (target[id].measurements > SAMPLES || runMode == RUN_MODE_POSITIONER)
		arUtilMatMul(target[id].transformation, target[id].marker_trans_inv, target[id].inferred_position);

    return;
}

bool agreeWithMajority(int id) {
	int count = 0;
	for (int i = 0; i < marker_num; i++) {
		int sid = marker_info[i].id;
		if (sid != -1 && target[sid].idx == i && (target[sid].measurements > SAMPLES || runMode == RUN_MODE_POSITIONER)) {
			bool ok = true;
			for (int j = 0; ok && j < 3; j++)
				if (fabs(target[id].inferred_position[j][3] - target[sid].inferred_position[j][3]) > CLOSE_MAT_THRESHOLD)
					ok = false;
			count += ok;
		}
	}
	int agreeWith = runMode == RUN_MODE_POSITIONER ? recognized_targets : measured_recognized_num;
	/*
	if (runMode == RUN_MODE_POSITIONER) {
		printf("%d agrees with %d out of %d.\n", id, count, recognized_targets);
	}
	*/

	//cout << id << " Agrees with " << count << " out of " << agreeWith << endl;
	return 2 * count >= agreeWith;
}

bool saneMatrix(double mat[3][4]) {
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			if (fabs(mat[i][j]) > 1e6)
				return false;
	return true;
}

void applyTrans(double next[3][4]) {
	lock_guard<mutex> lk(loc_mtx);
	memcpy(trans, next, sizeof trans);

	if (arFilterTransMat(transFilter, trans, !transValid) < 0)
		ARLOGe("arFilterTransMat error with camera transform.\n");
	transValid = true;
}