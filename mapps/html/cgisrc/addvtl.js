<!-- 
var vtltypes = new Array(
	new Array("ADIC Scalar 24", 0x1),
	new Array("ADIC Scalar 100", 0x2),
	new Array("ADIC Scalar i2000", 0x3),
	new Array("HP StorageWorks ESL9000", 0x4),
	new Array("HP StorageWorks ESL E-Series", 0x5),
	new Array("HP StorageWorks EML E-Series", 0x6),
	new Array("IBM 3583 Ultrium Scalable Library", 0x7),
	new Array("IBM 3584 Ultra Scalable Library", 0x8),
	new Array("IBM IBM System Storage TS3100", 0x9),
	new Array("HP StorageWorks MSL 2024/4048/8096", 0x0A),
	new Array("HP StorageWorks MSL 6000", 0x0B),
	new Array("Overland NEO 2000/4000/8000 Series", 0x0C)
);

var vtldrivetypes = new Array(
	new Array("ADIC Scalar 24", 0x07, 0x10, 0x11),
	new Array("ADIC Scalar 100", 0x07, 0x08, 0x10, 0x11, 0x12),
	new Array("ADIC Scalar i2000", 0x07, 0x08, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15),
	new Array("HP StorageWorks ESL9000", 0x06, 0x07, 0x09, 0x0B, 0xC),
	new Array("HP StorageWorks ESL E-Series", 0x04, 0x05, 0x09, 0x0B, 0x0C, 0x0D),
	new Array("HP StorageWorks EML E-Series", 0x0B, 0x0C, 0x0D),
	new Array("IBM 3583 Ultrium Scalable Library", 0x10, 0x11, 0x12),
	new Array("IBM 3584 Ultra Scalable Library", 0x10, 0x11, 0x12, 0x13, 0x14, 0x15),
	new Array("IBM System Storage TS3100", 0x10, 0x11, 0x12, 0x13, 0x14),
	new Array("HP StorageWorks MSL 2024/4048/8096", 0x9, 0xB, 0xC, 0xD, 0xE, 0xF),
	new Array("HP StorageWorks MSL 6000", 0x9, 0xB, 0xC, 0xD, 0xE, 0xF),
	new Array("Overland NEO 2000/4000/8000 Series", 0x9, 0xB, 0xC, 0xD, 0xE, 0xF)
);

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
	var frm = document.getElementById('addvtl');

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
	var frm = document.getElementById('addvtl');

	for (var i=0; i < drivetypes.length; i++)
	{
		var vtlval = drivetypes[i];

		if (i == 0)
			frm.vselect.options[i] = new Option(vtlval[0], vtlval[1], true, false);
		else
			frm.vselect.options[i] = new Option(vtlval[0], vtlval[1], false, false);
	}
	frm.drivetype0.options.length = 0;
}

function fillvtl()
{
	var frm = document.getElementById('addvtl');

	for (var i=0; i < vtltypes.length; i++)
	{
		var vtlval = vtltypes[i];

		if (i == 0)
			frm.vselect.options[i] = new Option(vtlval[0], vtlval[1], true, false);
		else
			frm.vselect.options[i] = new Option(vtlval[0], vtlval[1], false, false);
	}
}

function fillvtldrivetypes()
{
	var frm = document.getElementById('addvtl');
	var index = frm.vselect.selectedIndex;
	var i;
	var darray;

	frm.drivetype0.options.length = 0;

	if (index < 0)
		return;

	darray = vtldrivetypes[index];
	for (i = 1; i < darray.length; i++)
	{
		var dtype = darray[i] - 1;
		var vtlval = drivetypes[dtype];

		frm.drivetype0.options[i-1] = new Option(vtlval[0], vtlval[1], true, false);
	}
}

function fillinit()
{
	var frm = document.getElementById('addvtl');
	fillvtl(frm);
	frm.vselect.selectedIndex = 0;
	fillvtldrivetypes();
}

function filloptions()
{
	var frm = document.getElementById('addvtl');
	frm.vselect.options.length = 0;

	if (frm.vtype[0].checked == true)
	{
		fillvtl();
		frm.vselect.selectedIndex = 0;
		fillvtldrivetypes();
	}
	else
	{
		filldrive();
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

function get_voltype(drivetype)
{
	switch (parseInt(drivetype))
	{
		case 0x1:
			return 0x3;
		case 0x2:
			return 0x6;
		case 0x4:
			return 0x4;
		case 0x5:
			return 0x5;
		case 0x5:
			return 0x1;
		case 0x6:
			return 0x2;
		case 0x7:
			return 0x2; 
		case 0x8:
			return 0x1;
		case 0x9:
			return 0x2; 
		case 0xa:
			return 0x4; 
		case 0xb:
			return 0x5;
	}
	return 0;
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
	var frm = document.getElementById('addvtl');
	if (!frm.lname.value)
	{
		alert("VTL Name cannot be empty");
		return false;
	}

	if (!ValidString(frm.lname.value))
	{
		alert("VTL Name can only contain alphabets or numbers");
		return false;
	}

	if (!frm.ndrives.value)
	{
		alert("Number of VDrives cannot be empty\n");
		return false;
	}

	if (isNumeric(frm.ndrives.value) == false)
	{
		alert("Number of VDrives has invalid non numeric value\n");
		return false;
	}

	var ndrives = parseInt(frm.ndrives.value);

	if (ndrives == 0)
	{
		alert("Number of VDrives cannot be zero\n");
		return false;
	}
	else if (ndrives > 15)
	{
		alert("Number of VDrives greater than maximum VDrives per VTL\n");
		return false;
	}

	frm.ndrivetype0.value = frm.ndrives.value;
	return true;
}
// -->
