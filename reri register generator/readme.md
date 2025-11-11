## RERI Register Generator
RERI Register Generator는 xlsx (마이크로소프트 엑셀파일)로 작성되어, 쉽게 레지스터에 삽입할 값을 구하는데 사용됩니다.

전체적인 구성은 RERI Specification 기반의 Control Register, Status Register의 각 비트의 의미와 그걸 통한 이진수 변환. 16진수 변환으로 이루어져 있습니다.

해당 방식을 통해 QEMU 코드 상에 선언된 주소값에 직접적으로 devmem을 통한 값 삽입으로 에러를 시뮬레이트 할 수 있습니다.

기본적으로 스펙상에서 RERI Control_i Register는 Error bank +  64(offset) * (i + 1)의 주소를 가지며, 0x4010000 + 64 * (i + 1)을 코드상에서 가진다고 볼 수 있습니다. 그러므로 일반적으로 예시로 사용되는 Control_0 Register의 주소값은 0x4010040입니다.

기본적으로 스펙상에서 RERI Status_i Register는 Error bank + 72(offset) * (i + 1)의 주소를 가지며, 0x4010000 + 72 * (i + 1)을 코드상에서 가진다고 볼 수 있습니다. 그러므로 일반적으로 예시로 사용되는 Status_0 Register의 주소값은 0x4010048입니다.

### Control Register

<img width="1163" height="664" alt="Image" src="https://github.com/user-attachments/assets/362421cc-9bb2-4c21-87db-8890960ee076" />

### Status Register

<img width="1161" height="663" alt="Image" src="https://github.com/user-attachments/assets/4ac9539a-016d-41d5-b1fb-100b4bcbdb69" />

## Usage Example  
<img width="1201" height="344" alt="Image" src="https://github.com/user-attachments/assets/27d6ce84-f16e-479a-822f-07a600b23363" />
