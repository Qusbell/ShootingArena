※공통※
캐릭터가 가진 이동기(달리기, 대쉬, 파쿠르, 슬라이딩)의
모든 값들은 QuakeLike_Base → YJS → Data → Data_YJS_SpecialMovement 에서 수정이 가능합니다.
변수들 이름을 각 이동기에 알맞게 작성하였으니 이 글을 읽으시고 수정을 하시면 됩니다.
Interp Speed 같은 세부사항은 프로그래머가 직접 테스트하면서 바꾸어야 하기 때문에 변경하시기 전에 말씀 해주세요.

- 달리기 (왼쪽 Shifht) -
	Run Speed의 값을 변경하면 달리기 이동속도의 빠르기를 조절할 수 있습니다.

	Normal Speed값을 변경하면 기본 이동속도를 조절할 수 있습니다.

	※Normal Speed값은 BaseChar의 이동속도기준으로 설정을 해놓은거라 변경하기 전에 미리 말씀해주세요!

- 대쉬 (X키) -
	캐릭터를 멀리 나가게 할려면 Dash Strength의 값을 올리면 됩니다.

	Dashing Time을 변경하면 대쉬의 날라가는 지속 시간이 조절할 수 있습니다.

	Dash Delay Time을 변경하면 대쉬의 쿨타임을 조절할 수 있습니다.

- 파쿠르 (V키) -
	Character Look Point 값을 변경하면 캐릭터가 벽과의 어느정도 거리에서 파쿠르를 할건지 조절할 수 있습니다.

	Wall Offset값을 변경하면 캐릭터가 벽을 넘은 후 벽 안쪽으로 어느정도 들어갈건지 조절할 수 있습니다.

	Trace Height 값을 변경하면 캐릭터가 벽의 높이를 감지하기 위한 레이저를 쏠 떄 위로 어느정도 쏠건지 조절할 수 있습니다.

	Trace Down Height를 변경하면 캐릭터가 벽의 높이를 감지하기 위한 레이저를 쏜 후 아래로 어느정도 내릴건지 조절할 수 있습니다.

	Max Vault Height를 변경하면 캐릭터가 어느정도의 높이를 넘을 수 있을지를 조절할 수 있습니다.
	
- 슬라이딩 (C키) -
	Target Enlargement FOV를 변경하면 슬라이딩 도중 캐릭터의 시야각을 조절할 수 있습니다.

	Slide Ground Friction, Slide Slow Walking Braking을 변경하면 슬라이딩 도중 캐릭터의 지면 마찰력과 제동력을 조절할 수 있습니다.

	Slide Driving Force를 변경하면 슬라이딩할 때 받는 추진력을 조절할 수 있습니다.

	※Interp Speed를 변경하면 슬라이딩 중 카메라 시야각의 프레임 따라오는 값을 조절할 수 있습니다. -> 이부분 미리 말씀해주세요.

	※Target FOV는 기본 시야각을 가져온거기 때문에 변경하기 전에 미리 말씀해주세요!