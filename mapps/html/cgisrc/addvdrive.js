<!-- 
var drivetypes = new Array(
	new Array("HP StorageWorks DLT VS80", 0x1),
	new Array("HP StorageWorks DLT VS160", 0x2),
	new Array("HP StorageWorks SDLT 220", 0x3),
	new Array("HP StorageWorks SDLT 320", 0x4),
	new Array("HP StorageWorks SDLT 600", 0x5),
	new Array("Quantum SDLT 220", 0x6),
	new Array("Quantum SDLT 320", 0x7),
	new Array("Quantum SDLT 600", 0x8),
	new Array("HP StorageWorks Ultrium 232", 0x9),
	new Array("HP StorageWorks Ultrium 448", 0xA),
	new Array("HP StorageWorks Ultrium 460", 0xB),
	new Array("HP StorageWorks Ultrium 960", 0xC),
	new Array("HP StorageWorks Ultrium 1840", 0xD),
	new Array("HP StorageWorks Ultrium 3280", 0xE),
	new Array("HP StorageWorks Ultrium 6250", 0xF),
	new Array("IBM 3580 Ultrium1", 0x10),
	new Array("IBM 3580 Ultrium2", 0x11),
	new Array("IBM 3580 Ultrium3", 0x12),
	new Array("IBM 3580 Ultrium4", 0x13),
	new Array("IBM 3580 Ultrium5", 0x14),
	new Array("IBM 3580 Ultrium6", 0x15)
);

function disprefix()
{
	var frm = document.getElementById('addvdrive');

	if (frm.autofill.checked == true)
	{
		frm.prefix.disabled = false;
		frm.slots.disabled = false;
	}
	else
	{
		frm.prefix.disabled = true;
		frm.slots.disabled = true;
	}
}

function filldrive()
{
	var frm = document.getElementById('addvdrive');

	for (var i=0; i < drivetypes.length; i++)
	{
		var vtlval = drivetypes[i];

		if (i == 0)
			frm.drivetype.options[i] = new Option(vtlval[0], vtlval[1], true, false);
		else
			frm.drivetype.options[i] = new Option(vtlval[0], vtlval[1], false, false);
	}
}

function ValidString(str){
	var re = /[^a-zA-Z0-9]/

	if (re.test(str))
	{
		return false;
	}
	return true;
}

function isNumeric(str){
	var re = /[\D]/
	if (re.test(str))
	{
		return false;
	}
	return true;
}

function checkform()
{
	var frm = document.getElementById('addvdrive');

	if (!frm.name.value) {
		alert("VDrive Name cannot be empty");
		return false;
	}

	if (!ValidString(frm.name.value)) {
		alert("VDrive Name can only contain alphabets or numbers");
		return false;
	}

	return true;
}
// -->
